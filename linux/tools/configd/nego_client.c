#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/poll.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "xutils.h"
#include "reconfig.h"

#define BUFLEN 512
//#define PORT 9930
//#define SRV_IP "192.168.0.253"

extern sig_atomic_t sigint;
extern sig_atomic_t ready;

int negotiation_client(char sugg[MAXS][256], size_t used, char *fbname)
{
	int sock, ret, ack, seq, i, take;
//	struct sockaddr_in si_other;
	char buf[BUFLEN];
//	socklen_t slen = sizeof(si_other);
	struct pn_hdr *hdr;
	struct pn_hdr_compose *chdr;
	size_t len;
	struct pollfd fds;

	if (used < 1)
		panic("No suggestions available!\n");

	ready = 0;

	sock = open("/dev/lana_re_cfg", O_RDWR);
//	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0)
		panic("socket");

//	fbname[63] = 0;
	ret = ioctl(sock, 3221752320, fbname);
	if (ret < 0)
		panic("ioctl %s\n", strerror(errno));

	printd("ioctl done!\n");

//	memset((char *) &si_other, 0, sizeof(si_other));
//	si_other.sin_family = AF_INET;
//	si_other.sin_port = htons(PORT);

//	ret = inet_aton(SRV_IP, &si_other.sin_addr);
//	if (ret == 0) {
//		fprintf(stderr, "inet_aton() failed\n");
//		exit(1);
//	}

	hdr = (struct pn_hdr *) buf;
	hdr->seq = htons((uint16_t) random());
	hdr->ack = htons(0);
	hdr->type = TYPE_SUGGEST;
	len = sizeof(*hdr);

	for (i = 0; i < used; ++i) {
		strcpy(buf + len, sugg[i]);
		len += strlen(sugg[i]);
		buf[len] = 0;
		len++;
	}

	ack = ntohs(hdr->seq);

retry:
	if (sigint)
		return -EIO;

	printd("Doing write!\n");

//	ret = sendto(sock, buf, len, 0, (struct sockaddr *) &si_other, slen);
	ret = write(sock, buf, len);
	if (ret < 0) {
		printd("Cold not write!!!\n");
		close(sock);
		ready = 1;
		return ret;
	}

	printd("Sent suggesttions!\n");

	fds.fd = sock;
	fds.events = POLLIN;

	poll(&fds, 1, 1000 * 1);
	if ((fds.revents & POLLIN) != POLLIN) {
		printd("Timeout! Retry!\n");
		goto retry;
	}

	printd("Waiting for server response!\n");

//	ret = recvfrom(sock, buf, BUFLEN, 0, NULL, NULL);
	ret = read(sock, buf, BUFLEN);
	if (ret < sizeof(*hdr) + sizeof(*chdr)) {
		printd("Packet too small!!!! Retry!\n");
		goto retry;
		//panic("recvfrom(%s)", strerror(errno));
	}

	hdr = (struct pn_hdr *) buf;
	chdr = (struct pn_hdr_compose *) buf + sizeof(*hdr);
	if (ack != ntohs(hdr->ack)) {
		printd("Wrong ACK number, but okay for now....\n");
	//	goto retry;
	}
	seq = ntohs(hdr->seq);

	take = chdr->which;
	printf("Take %d!\n", take);

	//TODO:
	// build stack, create new hash on eth

	//send under new stack
//	hdr->type = TYPE_ACK;
//	hdr->ack = htons(seq);
//	hdr->seq = htons(ack + 1);

//	ret = sendto(sock, buf, sizeof(*hdr), 0, (struct sockaddr *) &si_other, slen);
//	ret = write(sock, buf, sizeof(*hdr));
//	if (ret < 0) {
//		printd("sth went wrong: %s\n", strerror(ret));
//		close(sock);
//		return ret;
//	}

	commit_vstack(NULL);
	printd("Communication now via new vstack!\n");

	close(sock);
	return take;
}
