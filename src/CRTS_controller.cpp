#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <signal.h>
#include <ctype.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "node_parameters.hpp"
#include "read_configs.hpp"

#define MAXPENDING 5

// global variables
int sig_terminate;
int num_nodes_terminated;

int Receive_msg_from_nodes(int *client, int num_nodes){
    // Listen to sockets for messages from any node
    char msg;
    for(int i=0; i<num_nodes; i++){
        int rflag = recv(client[i], &msg, 1, 0);
        int err = errno;
        
        // Handle errors
        if (rflag <= 0){ 
            if(!((err == EAGAIN) || (err == EWOULDBLOCK))){
                close(client[i]);
                printf("Node %i has disconnected. Terminating the current scenario..\n\n", i+1);
                //sig_terminate = 1;
                // tell all nodes to terminate program
                for(int j=0; j<num_nodes; j++){
                    write(client[j], &msg, 1);
                }
                return 1;
            }
        }    
        
        // Parse command if received a message
        else{
            switch (msg){
            case terminate_msg: // terminate program
                printf("Node %i has sent a termination message...\n", i+1);
                num_nodes_terminated++;
                // check if all nodes have terminated
                if(num_nodes_terminated == num_nodes) return 1;
                break;
            default:
                printf("Invalid message type received from node %i\n", i);
            }
        }
    }

    return 0;
}

void help_CRTS_controller() {
    printf("CRTS_controller -- Initiate cognitive radio testing.\n");
    printf(" -h : Help.\n");
    printf(" -m : Manual Mode - Start each node manually rather than have CRTS_controller do it automatically.\n");
    printf(" -a : IP Address - IP address of this computer as seen by remote nodes.\n");
    printf("      Autodetected by default.\n");
}

void terminate(int signum){
    printf("Terminating scenario on all nodes\n");
    sig_terminate = 1;    
}

int main(int argc, char ** argv){
    
    // register signal handlers
    signal(SIGINT, terminate);
    signal(SIGQUIT, terminate);
    signal(SIGTERM, terminate);
            
    sig_terminate = 0;
	
	int manual_execution = 0;

    // Use current username as default username for ssh
    char ssh_uname[LOGIN_NAME_MAX+1];
    getlogin_r(ssh_uname, LOGIN_NAME_MAX+1);

    // Use currnet location of CRTS Directory as defualt for ssh
    char crts_dir[1000];
    getcwd(crts_dir, 1000);

    // Default IP address of server as seen by other nodes
    char * serv_ip_addr;
    // Autodetect IP address as seen by other nodes
    struct ifreq interfaces;
    //Create a socket
    int fd_ip = socket(AF_INET, SOCK_DGRAM, 0);
    // For IPv4 Address
    interfaces.ifr_addr.sa_family = AF_INET;
    // Get Address associated with eth0
    strncpy(interfaces.ifr_name, "eth0", IFNAMSIZ-1);
    ioctl(fd_ip, SIOCGIFADDR, &interfaces);
    close(fd_ip);
    // Get IP address out of struct
    char serv_ip_addr_auto[30];
    strcpy(serv_ip_addr_auto, inet_ntoa(((struct sockaddr_in *)&interfaces.ifr_addr)->sin_addr) );
    serv_ip_addr = serv_ip_addr_auto;

    // interpret command line options
    int d;
    while((d = getopt(argc, argv, "hma:")) != EOF){
        switch (d){
            case 'h': help_CRTS_controller();   return 0;
            case 'm': manual_execution = 1;     break;
            case 'a': serv_ip_addr = optarg;    break;
        }
    }
    
    // Message about IP Address Detection
    printf("IP address of CRTS_controller autodetected as %s\n", serv_ip_addr_auto);
    printf("If this is incorrect, use -a option to fix.\n\n");

    // Create socket for incoming connections
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        printf("Transmitter Failed to Create Server Socket.\n");
        exit(EXIT_FAILURE);
    }
    // Allow reuse of a port. See http://stackoverflow.com/questions/14388706/socket-options-so-reuseaddr-and-so-reuseport-how-do-they-differ-do-they-mean-t
    // FIXME: May not be necessary in this version of CRTS
    int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1){
        printf("setsockopt() failed\n");
        exit(1);
    }
    // Construct local (server) address structure
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr)); // Zero out structure
    serv_addr.sin_family = AF_INET; // Internet address family
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Any incoming interface
    serv_addr.sin_port = htons(4444); // Local port
    // Bind to the local address to a port
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){
        printf("ERROR: bind() error\n");
        exit(1);
    }    

    // objects needs for TCP links to cognitive radio nodes
    int client[48];
    struct sockaddr_in clientAddr[48];
    socklen_t client_addr_size[48];
    struct node_parameters np[48];
    
    // read master scenario config file
    char scenario_list[30][60];
    unsigned int scenario_reps[60];
    int num_scenarios = read_scenario_master_file(scenario_list, scenario_reps);
    printf("Number of scenarios: %i\n\n", num_scenarios);

    // loop through scenarios
    for (int i = 0; i < num_scenarios; i++){
        for (unsigned int scenRepNum=1; scenRepNum<=scenario_reps[i]; scenRepNum++)
        {
            printf("Scenario %i:\n", i+1);
            printf("Rep %i:\n", scenRepNum);
            printf("Config file: %s\n", &scenario_list[i][0]);
            // read the scenario parameters from file
            struct scenario_parameters sp = read_scenario_parameters(&scenario_list[i][0]);
            // Set the number of scenario  repititions in struct.
            sp.totalNumReps = scenario_reps[i];
            sp.repNumber = scenRepNum;

            printf("Number of nodes: %i\n", sp.num_nodes);
            //printf("Run time: %f\n", sp.runTime);
            printf("Run time: %lld\n", (long long) sp.runTime);

            

            // determine the start time for the scenario based
            // on the current time and the number of nodes
            struct timeval tv;
            time_t time_s;
            gettimeofday(&tv, NULL);
            time_s = tv.tv_sec;
            int pad_s = manual_execution ? 120 : 1;
            sp.start_time_s = time_s + 1*sp.num_nodes + pad_s;
            
			// loop through nodes in scenario
            for (int j = 0; j < sp.num_nodes; j++){
                char node_id[10];
                snprintf(node_id, 10, "%d", j + 1);

                // read in node settings
                memset(&np[j], 0, sizeof(struct node_parameters));
                printf("Reading node %i's parameters...\n", j+1);
                np[j] = read_node_parameters(j+1, &scenario_list[i][0]);
                print_node_parameters(&np[j]);

                // If scenario is run more than once, append rep# to log file names
                if (scenario_reps[i]-1)
                {
                    snprintf(np[j].phy_rx_log_file+strlen(np[j].phy_rx_log_file), 100-strlen(np[j].phy_rx_log_file), "_rep%d", scenRepNum);
                    snprintf(np[j].phy_tx_log_file+strlen(np[j].phy_tx_log_file), 100-strlen(np[j].phy_tx_log_file), "_rep%d", scenRepNum);
                    snprintf(np[j].net_rx_log_file+strlen(np[j].net_rx_log_file), 100-strlen(np[j].net_rx_log_file), "_rep%d", scenRepNum);
                }
                
                // send command to launch executable if not doing so manually
                int ssh_return = 0;
                if (!manual_execution){
                    char command[2000] = "ssh "; 
                    // Add username to ssh command
                    strcat(command, ssh_uname);
                    strcat(command, "@");
                    strcat(command, np[j].CORNET_IP);
                    strcat(command, " 'sleep 1 && ");
                    strcat(command, " cd ");
                    strcat(command, crts_dir);
                    
					// add appropriate executable
                    switch (np[j].type){
                    case CR:
                        strcat(command, " && ./CRTS_CR");
                        break;
                    case interferer:
                        strcat(command, " && ./CRTS_interferer");
                        break;
                    }
            
                    // append IP Address of controller
                    strcat(command, " -a ");
                    strcat(command, serv_ip_addr);

                    // set command to continue program after shell is closed
                    strcat(command, " 2>&1 &'");
                    strcat(command, " > ");
                    strcat(command, crts_dir);
                    strcat(command, "/node");
                    strcat(command, node_id);
                    strcat(command, ".SYSOUT &");
                    ssh_return = system(command);
                    //printf("Command executed: %s\n", command);
                    //printf("Return value: %i\n", ssh_return);
                }

                if(ssh_return != 0){
                    printf("Failed to execute CRTS on node %i\n", j+1);
                    sig_terminate = 1;
                    break;
                }

                // listen for node to connect to server
                if (listen(sockfd, MAXPENDING) < 0)
                {
                    fprintf(stderr, "ERROR: Failed to Set Sleeping (listening) Mode\n");
                    exit(EXIT_FAILURE);
                }    
                
				// loop to accept
				// accept connection
				int accepted = 0;
                fd_set fds;
				struct timeval timeout;
				timeout.tv_sec = 1;
				timeout.tv_usec = 0;

				while(!sig_terminate && !accepted){
					FD_ZERO(&fds);
					FD_SET(sockfd, &fds);
					if(select(sockfd+1, &fds, NULL, NULL, &timeout) > 0){
						client[j] = accept(sockfd, (struct sockaddr *)&clientAddr[j], &client_addr_size[j]);
                		if (client[j] < 0)
                		{
                	    	fprintf(stderr, "ERROR: Sever Failed to Connect to Client\n");
                	    	exit(EXIT_FAILURE);
                		}

						accepted = 1;
					}
				}

				if(sig_terminate)
					break;

                // set socket to non-blocking
                fcntl(client[j], F_SETFL, O_NONBLOCK);

                // send scenario and node parameters
                printf("\nNode %i has connected. Sending its parameters...\n", j+1);
                char msg_type = scenario_params_msg;
                send(client[j], (void*)&msg_type, sizeof(char), 0);
                send(client[j], (void*)&sp, sizeof(struct scenario_parameters), 0);
                send(client[j], (void*)&np[j], sizeof(struct node_parameters), 0);            
            }

			// if in manual mode update the start time for all nodes
            if(manual_execution && !sig_terminate){

				struct timeval tv;
            	time_t time_s;
            	gettimeofday(&tv, NULL);
            	time_s = tv.tv_sec;
            	int pad_s = 3;
            	time_t start_time_s = time_s + 1*sp.num_nodes + pad_s;
            	
				// send updated start time to all nodes
				char msg_type = manual_start_msg;
            	for(int j=0; j<sp.num_nodes; j++){
					send(client[j], (void*)&msg_type, sizeof(char), 0);
            		send(client[j], (void*)&start_time_s, sizeof(time_t), 0);
               	}
			}

            printf("Listening for scenario termination message from nodes\n");

			int msg_terminate = 0;
            num_nodes_terminated = 0;
            while((!sig_terminate) && (!msg_terminate)){
                msg_terminate = Receive_msg_from_nodes(&client[0], sp.num_nodes);
            }

            if(msg_terminate)
                printf("Terminating controller because all nodes have sent a termination message\n");

            if(sig_terminate){
                char msg = terminate_msg;
                for(int j=0; j<sp.num_nodes; j++){
                    write(client[j], &msg, 1);
                }
            }

        }// scenario repition loop

		if(sig_terminate)
			break;

    }// scenario loop

}// main



















