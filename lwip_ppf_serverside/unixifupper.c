#include "lwip/debug.h"
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "lwip/stats.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "netif/list.h"
#include "netif/unixif.h"
#include "lwip/sys.h"
#include "lwip/timers.h"
#include "netif/tcpdump.h"
#include "unixifupper.h"
#include "lwip/opt.h"
#include "lwip/tcp.h"


#define UNIXIF_QUEUELEN 6
#define BACKLOG 5

#ifndef UNIXIF_DEBUG
#define UNIXIF_DEBUG LWIP_DBG_OFF
#endif

struct unixif_buf {
  struct pbuf *p;
  unsigned short len, tot_len;
  void *payload;
};

struct unixif {
  int fd;
  sys_sem_t sem;
  struct list *q;
  struct tcp_pcb *tpcb;
};

static int
unix_socket_client(const char *name)
{
  int fd;
#if !defined(linux) && !defined(cygwin) && !defined(__CYGWIN__)
  int len;
#endif
  struct sockaddr_un unix_addr;

                                /* create a Unix domain stream socket */
  if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    perror("unixif: unix_socket_client: socket");
    return(-1);
  }

                                /* fill socket address structure w/our address */
  memset(&unix_addr, 0, sizeof(unix_addr));
  unix_addr.sun_family = AF_UNIX;
  snprintf(unix_addr.sun_path, sizeof(unix_addr.sun_path), "%s%05d", "/var/tmp/", getpid());
#if !defined(linux) && !defined(cygwin) && !defined(__CYGWIN__)
  len = sizeof(unix_addr.sun_len) + sizeof(unix_addr.sun_family) +
    strlen(unix_addr.sun_path) + 1;
  unix_addr.sun_len = len;
#endif /* linux */

  unlink(unix_addr.sun_path);             /* in case it already exists */
  if (bind(fd, (struct sockaddr *) &unix_addr,
      sizeof(struct sockaddr_un)) < 0) {
    perror("unixif: unix_socket_client: socket");
    return(-1);
  }
  if (chmod(unix_addr.sun_path, S_IRWXU | S_IRWXO) < 0) {
    perror("unixif: unix_socket_client: socket");
    return(-1);
  }

                                /* fill socket address structure w/server's addr */
  memset(&unix_addr, 0, sizeof(unix_addr));
  unix_addr.sun_family = AF_UNIX;
  strcpy(unix_addr.sun_path, name);
#if !defined(linux) && !defined(cygwin) && !defined(__CYGWIN__)
  len = sizeof(unix_addr.sun_len) + sizeof(unix_addr.sun_family) +
    strlen(unix_addr.sun_path) + 1;  
  unix_addr.sun_len = len;
#endif /* linux */
  if (connect(fd, (struct sockaddr *) &unix_addr,
      sizeof(struct sockaddr_un)) < 0) {
    perror("unixif: unix_socket_client: socket");
    return(-1);
  }
  return(fd);
}



static void
unixif_input_handler(void *data)
{
  struct unixif *unixif;
  char buf[1532], *bufptr;
  int len, plen, rlen, wr_err;
  struct pbuf *p, *q;
  struct tcp_pcp *tpcb;
  
  /* Parse unixif from argument */

  unixif = (struct unixif *)data;

  /* Read lenght of payload chunk from socket */
  len = read(unixif->fd, &plen, sizeof(int));
  if (len == -1) {
    perror("unixif_irq_handler: read");
    abort();
  }

  LWIP_DEBUGF(UNIXIF_DEBUG, ("unixif_irq_handler: len == %d plen == %d bytes\n", len, plen));  
  if (len == sizeof(int)) {

    /* if (plen < 20 || plen > 1500) { */
    /*   LWIP_DEBUGF(UNIXIF_DEBUG, ("plen %d!\n", plen)); */
    /*   return; */
    /* } */
    
    printf("[+] Reading TCP payload from upper socket\n");

    len = read(unixif->fd, buf, plen);
    if (len == -1) {
      perror("unixif_irq_handler: read");
      abort();
    }

    // formated printout of packet payload
    int i=0;
    for(i=0; i < len; i++) {
      printf("%02x ", (*((char *)buf + i) & 0xff));
      if (((i + 1) % 8) == 0) {
        printf("\n");
      }
    }
      printf("\n");

	/* len = read(unixif->fd, buf, plen); */

	/* printf("Value: %s\n", buf); */

    /* p = pbuf_alloc(PBUF_LINK, len, PBUF_POOL); */

    /* if (p != NULL) { */
    /*   rlen = len; */
    /*   bufptr = buf; */
    /*   q = p; */
    /*   while (rlen > 0) { */
    /*     memcpy(q->payload, bufptr, rlen > q->len? q->len: rlen); */
    /*     rlen -= q->len; */
    /*     bufptr += q->len; */
    /*     q = q->next; */
    /*   } */
    /*   pbuf_realloc(p, len); */


    /* Input application-payload in lwip */
    //wr_err = tcp_write(unixif->tpcb, buf, len, 1);
    wr_err = tcp_write(unixif->tpcb, buf, len, 1);
    printf("[+] inserted TCP-payload into lwip\n");

  }
}

static void 
unixif_thread(void *arg)
{

  printf("unixif_thread for reading from socket \n");

  struct unixif *unixif;
  unixif = (struct unixif *)arg;

  LWIP_DEBUGF(UNIXIF_DEBUG, ("unixif_thread: started.\n"));

  while (1) {
    sys_sem_wait(&unixif->sem);
    
    /* Call input-handler function */
    unixif_input_handler(unixif);

  }

}

static void 
unixif_thread2(void *arg)
{

  struct unixif *unixif;
  fd_set fdset;

  unixif = (struct unixif *)arg;

  LWIP_DEBUGF(UNIXIF_DEBUG, ("unixif_thread2: started.\n"));


  while (1) {
    FD_ZERO(&fdset);
    FD_SET(unixif->fd, &fdset);

    if (select(unixif->fd + 1, &fdset, NULL, NULL, NULL) > 0) {
      sys_sem_signal(&unixif->sem);
    }
  }
}

#if LWIP_TCP
static err_t 
unixifupper_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{

  /* Callback function that will be called when new tcp data arrives from the lwip stack  */

  struct unixif *unixif;
  unixif = (struct unixif *)arg;

  /* Extracting metadata from current TCP-connection */
  unsigned short local_port = pcb->local_port;
  unsigned short remote_port = pcb->remote_port;
  ip_addr_t local_ip = pcb->local_ip;
  ip_addr_t remote_ip = pcb->remote_ip;
  int len;

  /* Initial state "active" */
  char state = 'a';

  if (p != NULL) {

    len = p->len;

    /* add size of 4-tuple to the pbuf size */
    len += 13;

    /* Write length of current packet on UDS */
    if (write(unixif->fd, &len, sizeof(int)) == -1) {
      perror("unixif_upper_output: write");
      abort();
    }

    if (write(unixif->fd, &state, sizeof(char)) == -1) {
      perror("unixif_upper_output: write");
      abort();
    }

    if (write(unixif->fd, &local_ip, sizeof(ip_addr_t)) == -1) {
      perror("unixif_upper_output: write");
      abort();
    }

    /* write size of pcb on uds */
    if (write(unixif->fd, &local_port, sizeof(unsigned short)) == -1) {
      perror("unixif_upper_output: write");
      abort();
    }
    
    if (write(unixif->fd, &remote_ip, sizeof(ip_addr_t)) == -1) {
      perror("unixif_upper_output: write");
      abort();
    }

    /* write pcb on uds to signal a newly established connection */
    if (write(unixif->fd, &remote_port, sizeof(unsigned short)) == -1) {
      perror("unixif_upper_output: write");
      abort();
    }  

    /* TODO see method unixif_output_timeout for correct buffer handling */
    if (write(unixif->fd, p->payload, p->len) == -1) {
      perror("unixif_upper_output: write");
      abort();
    }

    /* formated printout of packet payload */
    int i=0;
    for(i=0; i < p->len; i++) {
      printf("%02x ", (*((char *)p->payload + i) & 0xff));
      if (((i + 1) % 8) == 0) {
        printf("\n");
      }
    }
    printf("\n");

    printf("[+] Wrote TCP-payload on upper socket\n");

    if (err == ERR_OK && p != NULL) {
      tcp_recved(pcb, p->tot_len);
      pbuf_free(p);
    } else {
      pbuf_free(p);
    }
  }

  else {

    /* signaling message for closed TCP-connection */
    len = 13;

    /* Set state to "closed" */
    state = 'o';

    if (write(unixif->fd, &len, sizeof(int)) == -1) {
      perror("unixif_upper_output: write");
      abort();
    }

    if (write(unixif->fd, &state, sizeof(char)) == -1) {
      perror("unixif_upper_output: write");
      abort();
    }

    if (write(unixif->fd, &local_ip, sizeof(ip_addr_t)) == -1) {
      perror("unixif_upper_output: write");
      abort();
    }

    /* write size of pcb on uds */
    if (write(unixif->fd, &local_port, sizeof(unsigned short)) == -1) {
      perror("unixif_upper_output: write");
      abort();
    }
    
    if (write(unixif->fd, &remote_ip, sizeof(ip_addr_t)) == -1) {
      perror("unixif_upper_output: write");
      abort();
    }

    /* write pcb on uds to signal a newly established connection */
    if (write(unixif->fd, &remote_port, sizeof(unsigned short)) == -1) {
      perror("unixif_upper_output: write");
      abort();
    }  
    
  }
  
  return ERR_OK;
}

static err_t 
unixifupper_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{

  printf("[+] New TCP-Connection accepted \n");

  struct unixif *unixif;
  unixif = (struct unixif *)arg;

  unixif->tpcb = pcb;

  LWIP_UNUSED_ARG(err);

  int ftuple_len = 13;
  unsigned short local_port = pcb->local_port;
  unsigned short remote_port = pcb->remote_port;
  ip_addr_t local_ip = pcb->local_ip;
  ip_addr_t remote_ip = pcb->remote_ip;

  /* Initial state "newly created" */
  char state = 'c';

  /* TODO writeout length of 4-Tuple */
  if (write(unixif->fd, &ftuple_len, sizeof(int)) == -1) {
    perror("unixif_upper_output: write");
    abort();
  }  

  if (write(unixif->fd, &state, sizeof(char)) == -1) {
    perror("unixif_upper_output: write");
    abort();
  }

  if (write(unixif->fd, &local_ip, sizeof(ip_addr_t)) == -1) {
    perror("unixif_upper_output: write");
    abort();
  }

  if (write(unixif->fd, &local_port, sizeof(unsigned short)) == -1) {
    perror("unixif_upper_output: write");
    abort();
  }

  if (write(unixif->fd, &remote_ip, sizeof(ip_addr_t)) == -1) {
    perror("unixif_upper_output: write");
    abort();
  }

  /* write pcb on uds to signal a newly established connection */
  if (write(unixif->fd, &remote_port, sizeof(unsigned short)) == -1) {
    perror("unixif_upper_output: write");
    abort();
  }

  /* Pass unixif-struct to all callback-functions */
  tcp_arg(pcb, arg);
  tcp_sent(pcb, NULL);
  tcp_recv(pcb, unixifupper_recv);
  return ERR_OK;
}

void 
unixifupper_init(int port){

  struct unixif *unixif;
  struct tcp_pcb *pcb;
  struct sockaddr_un addr;
  socklen_t len;
  int fd;

  unixif = (struct unixif *)malloc(sizeof(struct unixif));
  if (!unixif) {
    return ERR_MEM;
  }

  unixif->fd = unix_socket_client("/tmp/lwip-upper");
  if (unixif->fd == -1) {
    perror("unixif_init");
    abort();
  }

  unixif->q = list_new(UNIXIF_QUEUELEN);

  /* Create semaphore for signaling the second reader-thread the presence of new data
     that can be read from the UDS */
  if(sys_sem_new(&unixif->sem, 0) != ERR_OK) {
    LWIP_ASSERT("Failed to create semaphore", 0);
  }

  sys_thread_new("unixif_thread", unixif_thread, unixif, DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);
  sys_thread_new("unixif_thread2", unixif_thread2, unixif, DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);

  pcb = tcp_new();
  tcp_bind(pcb, IP_ADDR_ANY, port);
  pcb = tcp_listen(pcb);
  tcp_arg(pcb, unixif);
  tcp_accept(pcb, unixifupper_accept);

}


#endif /* LWIP_TCP */


