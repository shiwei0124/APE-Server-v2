#include "common.h"
#include "events.h"
#include "socket.h"

void events_loop(ape_global *ape)
{
	int nfd, fd, bitev;
	void *attach;
	
	while(ape->is_running) {
		int i;
		
		if ((nfd = events_poll(&ape->events, 1)) == -1) {
			printf("events error\n");
			continue;
		}
		for (i = 0; i < nfd; i++) {
			attach 	= events_get_current_fd(&ape->events, i);
			bitev 	= events_revent(&ape->events, i);
			fd	= ((ape_fds *)attach)->fd; /* assuming that ape_fds is the first member */
			switch(((ape_fds *)attach)->type) {
			
			case APE_SOCKET:
				switch(APE_SOCKET(attach)->type) {
				
				case APE_SOCKET_SERVER:
					if (bitev & EVENT_READ) {
						if (APE_SOCKET(attach)->proto == APE_SOCKET_TCP) {
							ape_socket_accept(APE_SOCKET(attach), ape);
						} else {							
							printf("read on UDP\n");
						}
					}
					break;
				case APE_SOCKET_CLIENT:
					if (bitev & EVENT_READ &&
						ape_socket_read(APE_SOCKET(attach), ape) == -1) {
						
						continue; /* ape_socket is free'ed */
					}
					
					if (bitev & EVENT_WRITE) {
						if (APE_SOCKET(attach)->state == APE_SOCKET_PROGRESS) {
							int serror = 0, ret;
							socklen_t serror_len = sizeof(serror);
							
							if ((ret = getsockopt(fd, SOL_SOCKET, SO_ERROR, &serror, &serror_len)) == 0 && 
								serror == 0) {
								
								APE_SOCKET(attach)->state = APE_SOCKET_ONLINE;
								
								printf("Success connect\n");
							} else {
								printf("Failed to connect\n");
							}
						} else if (APE_SOCKET(attach)->state == APE_SOCKET_ONLINE) {
							/* Do we have something to send ? */
							printf("[Socket] Rdy to send %i\n", APE_SOCKET(attach)->s.fd);
						}
					}

					break;
				case APE_SOCKET_UNKNOWN:
					break;
				}
				
				break;
			case APE_FILE:
				break;
			case APE_DELEGATE:
				((struct _ape_fd_delegate *)attach)->on_io(fd, bitev, ape); /* punning */
				break;
			}
		}

	}
}
