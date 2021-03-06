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

#include "netio.h"

#include "lwip/opt.h"
#include "lwip/tcp.h"

#define UNIXIF_BPS 512000
#define UNIXIF_QUEUELEN 6
/*#define UNIXIF_DROP_FIRST      */

#ifndef UNIXIF_DEBUG
#define UNIXIF_DEBUG LWIP_DBG_OFF
#endif

#define SV_SOCK_PATH "/tmp/unixif"

struct unixif_buf {
  struct pbuf *p;
  unsigned short len, tot_len;
  void *payload;
};

struct unixif {
  /* file descriptor */
  int fd;
  sys_sem_t sem;
  struct list *q;
};

#define BACKLOG 5


/* See http://www.nwlab.net/art/netio/netio.html to get the netio tool */


/*-----------------------------------------------------------------------------------*/
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


/*-----------------------------------------------------------------------------------*/
static int
unix_socket_server(const char *name)
{
  int fd;
  struct sockaddr_un unix_addr;

  /* create a Unix domain stream socket */
  if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    perror("unixif: unix_socket_server: socket");
    return(-1);
  }

  unlink(name);   /* in case it already exists */

  /* fill in socket address structure */
  memset(&unix_addr, 0, sizeof(unix_addr));
  unix_addr.sun_family = AF_UNIX;
  strcpy(unix_addr.sun_path, name);


  /* bind the name to the descriptor */
  if (bind(fd, (struct sockaddr *) &unix_addr,
      sizeof(struct sockaddr_un)) < 0) {
    perror("unixif: unix_socket_server: bind");
    return(-1);
  }

  /* if (chmod(unix_addr.sun_path, S_IRWXU | S_IRWXO) < 0) { */
  /*   perror("unixif: unix_socket_server: chmod"); */
  /*   return(-1); */
  /* } */


  if (listen(fd, 5) < 0) {  /* tell kernel we're a server */
    perror("unixif: unix_socket_server: listen");
    return(-1);
  }
  
  return(fd);
}

/*-----------------------------------------------------------------------------------*/
/* static int */
/* unix_socket_client(const char *name) */
/* { */
/*   int fd; */
/* #if !defined(linux) && !defined(cygwin) && !defined(__CYGWIN__) */
/*   int len; */
/* #endif */
/*   struct sockaddr_un unix_addr; */

/*                                 /\* create a Unix domain stream socket *\/ */
/*   if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) { */
/*     perror("unixif: unix_socket_client: socket"); */
/*     return(-1); */
/*   } */

/*                                 /\* fill socket address structure w/our address *\/ */
/*   memset(&unix_addr, 0, sizeof(unix_addr)); */
/*   unix_addr.sun_family = AF_UNIX; */
/*   snprintf(unix_addr.sun_path, sizeof(unix_addr.sun_path), "%s%05d", "/var/tmp/", getpid()); */
/* #if !defined(linux) && !defined(cygwin) && !defined(__CYGWIN__) */
/*   len = sizeof(unix_addr.sun_len) + sizeof(unix_addr.sun_family) + */
/*     strlen(unix_addr.sun_path) + 1; */
/*   unix_addr.sun_len = len; */
/* #endif /\* linux *\/ */

/*   unlink(unix_addr.sun_path);             /\* in case it already exists *\/ */
/*   if (bind(fd, (struct sockaddr *) &unix_addr, */
/*       sizeof(struct sockaddr_un)) < 0) { */
/*     perror("unixif: unix_socket_client: socket"); */
/*     return(-1); */
/*   } */
/*   if (chmod(unix_addr.sun_path, S_IRWXU | S_IRWXO) < 0) { */
/*     perror("unixif: unix_socket_client: socket"); */
/*     return(-1); */
/*   } */

/*                                 /\* fill socket address structure w/server's addr *\/ */
/*   memset(&unix_addr, 0, sizeof(unix_addr)); */
/*   unix_addr.sun_family = AF_UNIX; */
/*   strcpy(unix_addr.sun_path, name); */
/* #if !defined(linux) && !defined(cygwin) && !defined(__CYGWIN__) */
/*   len = sizeof(unix_addr.sun_len) + sizeof(unix_addr.sun_family) + */
/*     strlen(unix_addr.sun_path) + 1;   */
/*   unix_addr.sun_len = len; */
/* #endif /\* linux *\/ */
/*   if (connect(fd, (struct sockaddr *) &unix_addr, */
/*       sizeof(struct sockaddr_un)) < 0) { */
/*     perror("unixif: unix_socket_client: socket"); */
/*     return(-1); */
/*   } */
/*   return(fd); */
/* } */

/* Input handler function which is called, when a new payload can be read
   from the Unix Domain Socket */

static void
unixif_input_handler(void *data)
{

  printf("unixif_input_handler called \n");

  struct unixif *unixif;
  char buf[1532], *bufptr;
  int len, plen, rlen, wr_err;
  struct pbuf *p, *q;
  struct tcp_pcp *tpcb;

  /* Parse unixif from argument */
  unixif = (struct unixif *)data;

  /* Read-int tcp_pcp */
  /* size of this struct */

  /* Chunk-length ? */
  /* forward pointer as *ptr = len + size_of_pcb */
  len = read(unixif->fd, &plen, sizeof(int));
  if (len == -1) {
    perror("unixif_irq_handler: read");
    abort();
  }

  printf("read lenght\n");
  printf("unixif_input_handler: len == %d plen == %d bytes | %x \n", len, plen, plen); 

  LWIP_DEBUGF(UNIXIF_DEBUG, ("unixif_irq_handler: len == %d plen == %d bytes\n", len, plen));  
  if (len == sizeof(int)) {
    

    if (plen < 20 || plen > 1500) {
      printf("too long! plen %d!\n", plen);
      return;
    }


    /* Read data from unixif-socket */
    /* read(filedescriptor, buffer to store, length of chunk) : returns the amount of bytes which were read */
    len = read(unixif->fd, buf, plen);
    if (len == -1) {
      perror("[+] read data from uds");
      sys_sem_signal(unixif->sem);
      abort();
    }

    printf("unixif_irq_handler: read %d bytes\n", len);
 
    if (p != NULL) {
      rlen = len;
      bufptr = buf;
      q = p;
      while (rlen > 0) {
        memcpy(q->payload, bufptr, rlen > q->len? q->len: rlen);
        rlen -= q->len;
        bufptr += q->len;
        q = q->next;
      }
      pbuf_realloc(p, len);
      
      // formated printout of packet payload
      int i=0;
      for(i=0; i < p->len; i++) {
        printf("%02x ", (*((char *)p->payload + i) & 0xff));
        if (((i + 1) % 8) == 0) {
          printf("\n");
        }
      }
      printf("\n");


    } else {
      LWIP_DEBUGF(UNIXIF_DEBUG, ("unixif_irq_handler: could not allocate pbuf\n"));
    }


    /* 
    p = pbuf_alloc(PBUF_LINK, len, PBUF_POOL);

    if (p != NULL) {
      rlen = len;
      bufptr = buf;
      q = p;
      while (rlen > 0) {
        memcpy(q->payload, bufptr, rlen > q->len? q->len: rlen);
        rlen -= q->len;
        bufptr += q->len;
        q = q->next;
      }
      pbuf_realloc(p, len);
      LINK_STATS_INC(link.recv);
      tcpdump(p);
      */

    /* Input application-payload in lwip */
    wr_err = tcp_write(tpcb, buf, len, 1);

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
static err_t netio_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{

  printf("netio_recv \n");

  struct unixif *unixif;
  unixif = (struct unixif *)arg;


  if(write(unixif->fd, p->payload, p->len) != p->len){

    /* send only payload */
    printf("error \n");
    //sys_sem_signal(&unixif->sem);
  }

  /* int len = p->tot_len; */
  /* if (write(unixif->fd, &len, sizeof(int)) == -1) { */
  /*   perror("unixif_output: write"); */
  /*   abort(); */
  /* } */

  // TODO see method unixif_output_timeout for correct buffer handling
  //if (write(unixif->fd, data, p->tot_len) == -1) {
  /* if (write(unixif->fd, p->payload, p->tot_len) == -1) { */
  /*   perror("unixif_output: write"); */
  /*   abort(); */
  /* } */


  /* sys_sem_signal(&unixif->sem); */
  /* write packet on socket */
  /* TODO write tcp_pcb also on socket */
  /* if(write(unixif->fd, p->payload, p->len) != p->len){ */

    /* send only payload */
    /* /\* printf("error \n"); *\/ */
    /* printf("error \n"); */
    //sys_sem_signal(&unixif->sem);
  /* } */

  /* printf("[+] Wrote packet from lwip on uds\n"); */

  /* sys_sem_free(&unixif->sem); */
  /*
  struct unixif *unixif;
  unixif = (struct unixif *)arg;

  printf("netio_recv \n");
  printf("tcp-payload \n");

  Printout Application-Payload Data on Console 
  int i=0;
  for(i=0; i < p->len; i++) {
    printf("%02x ", (*((char *)p->payload + i) & 0xff));
    if (((i + 1) % 8) == 0) {
      printf("\n");
    }
  }
  printf("\n"); */

  /* Write received Application-Payload on Unix-Domain-Socket */

  /*
  LWIP_UNUSED_ARG(arg);

  if (err == ERR_OK && p != NULL) {
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
  } else {
    pbuf_free(p);
  }

  if (err == ERR_OK && p == NULL) {
    tcp_arg(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_close(pcb);
  }
  */



  return ERR_OK;
}



static err_t netio_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{

  printf("netio_accept \n");

  //LWIP_UNUSED_ARG(arg);
  LWIP_UNUSED_ARG(err);

  /* Pass unixif-struct to all callback-functions */
  tcp_arg(pcb, arg);
  tcp_sent(pcb, NULL);
  tcp_recv(pcb, netio_recv);
  return ERR_OK;
}

void netio_init(void){



  struct unixif *unixif;
  struct tcp_pcb *pcb;
  struct sockaddr_un addr;
  socklen_t len;
  int fd, fd2;


  unixif = (struct unixif *)malloc(sizeof(struct unixif));
  if (!unixif) {
    return ERR_MEM;
  }

  unixif->fd = unix_socket_server("/tmp/unixif");
  if (unixif->fd == -1) {
    perror("unixif_init");
    abort();
  }

  unixif->q = list_new(UNIXIF_QUEUELEN);

  if(sys_sem_new(&unixif->sem, 0) != ERR_OK) {
    LWIP_ASSERT("Failed to create semaphore", 0);
  }

  sys_thread_new("unixif_thread", unixif_thread, unixif, DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);
  sys_thread_new("unixif_thread2", unixif_thread2, unixif, DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);


  pcb = tcp_new();
  tcp_bind(pcb, IP_ADDR_ANY, 80);
  pcb = tcp_listen(pcb);
  tcp_arg(pcb, unixif);
  tcp_accept(pcb, netio_accept);

  return ERR_OK;  

}


#endif /* LWIP_TCP */


