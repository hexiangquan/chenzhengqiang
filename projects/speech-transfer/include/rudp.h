#ifndef  RUDP_PROTO_H
#define	RUDP_PROTO_H

#define RUDP_VERSION	1	/* Protocol version */
#define RUDP_MAXPKTSIZE 1000	/* Number of data bytes that can sent in a packet, RUDP header not included */
#define RUDP_MAXRETRANS 5	/* Max. number of retransmissions */
#define RUDP_TIMEOUT	2000	/* Timeout for the first retransmission in milliseconds */
#define RUDP_WINDOW	3	/* Max. number of unacknowledged packets that can be sent to the network*/

/* Packet types */

#define RUDP_DATA	1
#define RUDP_ACK	2
#define RUDP_SYN	4
#define RUDP_FIN	5

/*
 * Sequence numbers are 32-bit integers operated on with modular arithmetic.
 * These macros can be used to compare sequence numbers.
 */

#define	SEQ_LT(a,b)	((short)((a)-(b)) < 0)
#define	SEQ_LEQ(a,b)	((short)((a)-(b)) <= 0)
#define	SEQ_GT(a,b)	((short)((a)-(b)) > 0)
#define	SEQ_GEQ(a,b)	((short)((a)-(b)) >= 0)

/* RUDP packet header */

struct rudp_hdr {
	u_int16_t version;
	u_int16_t type;
	u_int32_t seqno;
}__attribute__ ((packed));



static const int  DROP=0;


#define LISTEN		0
#define SYN_SENT 	1
#define ESTABLISH	2
#define CLOSE_WAIT	3
#define CLOSED		4
int recvhandler(int fd, void *arg);
ssize_t sendtowithdropsendto(int sockfd, const void *buf, size_t len, int flags,const struct sockaddr *dest_addr, socklen_t addrlen);
#endif /* RUDP_PROTO_H */