#include "worker.h"

extern char *gcheckphrase;
extern unsigned long gprbs_seed;
unsigned long grx_saved_checksum = 0xff;

void rxworker(struct rxworker_s *rxworker) {
  int readsize;
  int restartme = 0;
  int readlen; // XXX
  char *buffer;
  char okphrase[] = "ok";
  char checkphrase[] = "yoes";
  buffer = calloc(1, (size_t)kfootsize);

  while (!rxworker->rxconf_parent->done_mbox) {
    restartme = 0;
    assert(rxworker->id < kthreadmax);
    pthread_mutex_lock(&rxworker->rxconf_parent->sa_mutex);
    whisper(7, "rxw:%02i accepting and locked\n", rxworker->id);
    rxworker->sockfd = tcp_accept(&(rxworker->rxconf_parent->sa),
                                  rxworker->rxconf_parent->socknum);
    whisper(5, "rxw:%02i accepted fd:%i \n", rxworker->id, rxworker->sockfd);
    DTRACE_PROBE1(viamillipede, worker__connected, rxworker->sockfd);
    pthread_mutex_unlock(&rxworker->rxconf_parent->sa_mutex);
    whisper(16, "rxw:%02i fd:%i expect %s\n", rxworker->id, rxworker->sockfd,
            gcheckphrase);
    read(rxworker->sockfd, buffer,
         (size_t)4); // XXX this is vulnerable to slow starts
    if (bcmp(buffer, gcheckphrase, 4) != 0) {
      whisper(1, "rxw:%02d checkphrase failure. This connection is not for me. "
                 "got: %x %x %x %x",
              rxworker->id, buffer[0], buffer[1], buffer[2], buffer[4]);
      exit(EDOM);
    }
    checkperror("checkphrase  nuiscance ");
    if (write(rxworker->sockfd, okphrase, (size_t)2) != 2) {
      exit(ENOTCONN);
    }
    checkperror("signaturewrite ");
    // /usr/src/contrib/netcat has a nice pipe input routine XXX perhaps lift it
    while (!rxworker->rxconf_parent->done_mbox && (!restartme)) {
      struct millipacket_s pkt;
      int cursor = 0;
      int preamble_cursor = 0;
      int preamble_fuse = 0;
      unsigned long rx_checksum = 0;
      while (preamble_cursor < sizeof(struct millipacket_s) && (!restartme)) {
        // fill the preamble + millipacket structure whole
        checkperror("nuicsance before preamble read");
        whisper(((errno == 0) ? 19 : 3),
                "rxw:%02i prepreamble and millipacket: errno:%i cursor:%i\n",
                rxworker->id, errno, preamble_cursor);
        struct iovec rxiov;
        rxiov.iov_len = (sizeof(struct millipacket_s) - preamble_cursor);
        rxiov.iov_base = ((u_char *)&pkt) + preamble_cursor;
        readlen = readv(rxworker->sockfd, &rxiov, 1);
        checkperror("preamble read");
        whisper(
            ((errno == 0) ? 19 : 3),
            "rxw:%02i preamble and millipacket: len %i errno:%i cursor:%i\n",
            rxworker->id, readlen, errno, preamble_cursor);
        assert((readlen >= 0) && " bad packet header read");
        if (readlen == 0) {
          whisper(6, "rxw:%02i exits after empty preamble\n", rxworker->id);
          pthread_exit(rxworker); // no really we are done, and who wants our
                                  // exit status?
          continue;
        };
        if (readlen < (sizeof(struct millipacket_s) - preamble_cursor)) {
          whisper(7, "rx: short header %i(read)< %lu (headersize), preamble "
                     "cursor: %i \n",
                  readlen, sizeof(struct millipacket_s), preamble_cursor);
          checkperror("short read");
        }
        preamble_cursor += readlen;
        assert(preamble_cursor <= sizeof(struct millipacket_s));
        preamble_fuse++;
        if (preamble_fuse > 100000) {
          whisper(1, "fuse popped waiting for a preamble, check network");
          exit(ETIMEDOUT);
        }
      }
      assert(preamble_cursor == sizeof(struct millipacket_s));
      assert(pkt.preamble == preamble_cannon_ul && "preamble check");
      assert(pkt.size >= 0);
      assert(pkt.size <= kfootsize);
      whisper(9, "rxw:%02i leg:%lu siz:%lu op:%x caught new leg\n",
              rxworker->id, pkt.leg_id, pkt.size, pkt.opcode);
      int remainder = pkt.size;
      int remainder_counter = 0;
      assert(remainder <= kfootsize);
      while (remainder && (!restartme)) {
        readsize =
            read(rxworker->sockfd, buffer + cursor, MIN(remainder, MAXBSIZE));
        checkperror("rx: read failure\n");
        if (errno != 0 || readsize <= 0) {
          whisper(4, "rxw:%02i retired due to read len:%i errno:%i\n",
                  rxworker->id, readsize, errno);
          restartme = 1;
        }
        cursor += readsize;
        assert(cursor <= kfootsize);
        remainder -= readsize;
        assert(remainder >= 0);
        if (readsize == 0 && (!restartme)) {
          whisper(2, "rx: 0 byte read ;giving up. are we done?\n"); // XXX this
                                                                    // should
                                                                    // not be
                                                                    // the end
          break;
        }
        checkperror("read buffer");

        whisper((errno != 0) ? 3 : 19, "rx%02i %lu[%i]-[%i]\t%c", rxworker->id,
                pkt.leg_id, readsize >> 10, remainder >> 10,
                ((remainder_counter++) % 16 == 0) ? (int)10 : (int)' ');
      }
      whisper(18, "\nrxw:%02i leg:%lu buffer filled to :%i\n", rxworker->id,
              pkt.leg_id, cursor);

      if (gprbs_seed > 0) {
        if (!prbs_verify((unsigned long *)buffer, gprbs_seed + pkt.leg_id,
                         kfootsize)) {
          whisper(1, "prbs verification failure");
          exit(EDOOFUS);
        }
      }

      checkperror("read leg");
      if (pkt.leg_id == 0) {
        initiate_relay(); // initiate the tcp connection if we are an initiator
      }
      /*block until the sequencer is ready to push this
       XXXX  suboptimal  sequencer ?? prove it!
       perhaps a minheap??

      Heisenberg compensator theory of operation:
      next_leg will monotonically increment asserting that the output stream is
      ordered by tracking it's assingment from the ingest code.

      If the sequencer blocks for an extended time; it's unlikely to ever get
      better so declare an error and exit
      */
      long sequencer_stalls = 0;
      while (pkt.leg_id != rxworker->rxconf_parent->next_leg && (!restartme)) {
        pthread_mutex_unlock(&rxworker->rxconf_parent->rxmutex);
#define ktimeout_sec (35)
#define ktimeout_granularity_usec (256)
#define ktimeout_stall_tolerance                                               \
  ((ktimeout_sec * 1000000) / ktimeout_granularity_usec)
        usleep(ktimeout_granularity_usec);
        sequencer_stalls++;
        if (sequencer_stalls > ktimeout_stall_tolerance) {
          whisper(1, "transporter accident! rx seqencer stalled");
          exit(EIO);
        }
        pthread_mutex_lock(
            &rxworker->rxconf_parent
                 ->rxmutex); // do nothing but compare seqeuncer under lock
      }
      pthread_mutex_unlock(&rxworker->rxconf_parent->rxmutex);
      if (!restartme) {
        whisper(5, "rxw:%02i sequenced leg:%08lu[%08lu]after %05ld stalls\n",
                rxworker->id, pkt.leg_id, pkt.size, sequencer_stalls);
        if (!gprbs_seed) {
          // we are verifying prbs, don't output
          int writesize = 0;
          struct iovec iov;
          iov.iov_len = pkt.size;
          iov.iov_base = (void *)buffer;
          writesize = writev(rxworker->rxconf_parent->output_fd, &iov, 1);
          whisper(19, "rxw: writev fd:%i siz:%ld",
                  rxworker->rxconf_parent->output_fd, pkt.size);
          checkperror("rxw:write buffer");
          assert(writesize == pkt.size);
        }
        DTRACE_PROBE(viamillipede, leg__rx);
        if (pkt.opcode == end_of_millipede) {
          whisper(5, "rxw:%02i caught 0x%x done with last frame\n",
                  rxworker->id, pkt.opcode);
          pthread_mutex_lock(&rxworker->rxconf_parent->rxmutex);
          rxworker->rxconf_parent->done_mbox = 1;
          pthread_mutex_unlock(&rxworker->rxconf_parent->rxmutex);
        } else if (pkt.opcode == feed_more) {
          // the last frame will be empty and have a borken cksum
          if (pkt.checksum) {
            rx_checksum =
                mix(grx_saved_checksum + pkt.leg_id, buffer, pkt.size);
            if (rx_checksum != pkt.checksum) {
              whisper(2, "rx checksum mismatch %lu != %lu", rx_checksum,
                      pkt.checksum);
              whisper(2, "rxw:%02i offending leg:%lu.%lu after %ld stalls\n",
                      rxworker->id, pkt.leg_id, pkt.size, sequencer_stalls);
            }
            assert(rx_checksum == pkt.checksum);
            grx_saved_checksum = rx_checksum;
          };
        } else {
          whisper(1, "bogus opcode %x", pkt.opcode);
          exit(EBADRPC);
        }
        pthread_mutex_lock(&rxworker->rxconf_parent->rxmutex);
        rxworker->rxconf_parent
            ->next_leg++; // do this last or race out the cksum code
        pthread_mutex_unlock(&rxworker->rxconf_parent->rxmutex);
      } // if not restartme
    }   // while !done_mbox
    whisper(5, "rxw:%02i exiting work restartme block\n", rxworker->id);
  } // restartme
  free(buffer);
  whisper(4, "rxw:%02i done", rxworker->id);
}

void rxlaunchworkers(struct rxconf_s *rxconf) {
  int worker_cursor = 0;
  int retcode;
  rxconf->done_mbox = 0;
  rxconf->next_leg = 0; // initalize sequencer
  if (tcp_recieve_prep(&(rxconf->sa), &(rxconf->socknum), rxconf->port) != 0) {
    whisper(1, "rx: tcp prep failed. this is unfortunate. ");
    exit(ENOTSOCK);
  }
  do {
    rxconf->workers[worker_cursor].id = worker_cursor;
    rxconf->workers[worker_cursor].rxconf_parent = rxconf;
    // all the bunnies made to hassenfeffer
    retcode =
        pthread_create(&rxconf->workers[worker_cursor].thread,
                       NULL, // attrs - none
                       (void *)&rxworker, &(rxconf->workers[worker_cursor]));
    whisper(18, "rxw:%02i threadlaunched\n", worker_cursor);
    checkperror("rxpthreadlaunch");
    assert(retcode == 0 && "pthreadlaunch");
    worker_cursor++;
  } while (worker_cursor < (kthreadmax));
  whisper(7, "rx: worker group launched\n");
}
int rx_poll(struct rxconf_s *rxconf) {
  /** return true if done */
  int ret_done;
  pthread_mutex_lock(&(rxconf->rxmutex));
  ret_done = rxconf->done_mbox;
  pthread_mutex_unlock(&(rxconf->rxmutex));
  return ret_done;
}
void rx(struct rxconf_s *rxconf) {
  pthread_mutex_init(&(rxconf->sa_mutex), NULL);
  pthread_mutex_init(&(rxconf->rxmutex), NULL);
  rxconf->done_mbox = 0;
  rxlaunchworkers(rxconf);
}
