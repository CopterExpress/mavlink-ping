#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>
#include <sysexits.h>
#include <string.h>
#include <netinet/in.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/timerfd.h>
#include <sys/time.h>
#include <mavlink_types.h>
#include <math.h>

#include "common/mavlink.h"
#include "common/mavlink_msg_ping.h"

// unistd optarg externals for arguments parsing
extern char *optarg;
extern int optind, opterr, optopt;
//

// Volatile flag to stop application (from signal)
static volatile sig_atomic_t stop_application = false;

// Linux signal handler (for SIGINT and SIGTERM)
static void signal_handler(int signum)
{
    // Set application stop flag
    stop_application = true;
}

static uint64_t get_absolute_now() {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return tv.tv_sec * (uint64_t)1000000 + tv.tv_usec;
}

// Remote address
static struct sockaddr_in remote_addr;
static uint32_t seq;
static int udp_socket_fd;
static int timer_fd;
static struct itimerspec timeout_spec;
enum {
    WAITING_RESPONSE,
    IDLE
} state;
static struct timespec ping_stamp;

static int send_ping()
{
    // Data write buffer
    uint8_t write_buf[MAVLINK_MAX_PACKET_LEN];
    mavlink_message_t ping_message;

    mavlink_msg_ping_pack(SOURCE_MAVLINK_ID, SOURCE_MAVLINK_COMPONENT, &ping_message, get_absolute_now(), seq, 0, 0);
    uint16_t output_size = mavlink_msg_to_send_buffer(write_buf, &ping_message);
    ssize_t ret = sendto(udp_socket_fd, &write_buf, output_size, 0, (struct sockaddr *)&remote_addr,
                 sizeof(remote_addr));
    if (ret < 0)
    {
        printf("Failed to write data to UDP: %s\n", strerror(errno));
        return -1;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &ping_stamp) < 0)
    {
        printf("Failed to get current time: %s\n", strerror(errno));
        return -1;
    }

    state = WAITING_RESPONSE;

    if (timerfd_settime(timer_fd, 0, &timeout_spec, NULL) < 0)
    {
        printf("Failed to start timeout timer: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {
    printf("MAVLink ping utility v%u.%u\n", VERSION_MAJOR, VERSION_MINOR);

    // Signal action structure
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = signal_handler;

    // Bind SIGINT and SIGTERM to the application signal handler
    if ((sigaction(SIGTERM, &act, 0) < 0) ||
        (sigaction(SIGINT, &act, 0) < 0))
    {
        printf("Error setting signal handler: %s\n", strerror(errno));

        return EX_OSERR;
    }

    char ip[INET_ADDRSTRLEN + 1] = {'\0'};
    unsigned long port = 0;
    unsigned long count = 0;
    float interval = 1.0f;
    float timeout = 1.0f;
    unsigned long target_id;
    unsigned long target_component;
    bool debug = false;

    int option;
    // For every command line argument
    while ((option = getopt(argc, argv, "dht:i:c:I:p:")) != -1)
        switch (option)
        {
            // Debug output
            case 'd':
                debug = true;
                break;
            // Ping count
            case 'c':
                count = atoi(optarg);
                if (count <= 0.0f)
                {
                    printf("\nInvalid ping count: \"%s\"!\n", optarg);
                    return EX_USAGE;
                }
                break;
            // Interval between pings
            case 'i':
                interval = atof(optarg);
                if (interval <= 0.0f)
                {
                    printf("\nInvalid interval between pings: \"%s\"!\n", optarg);
                    return EX_USAGE;
                }
                break;
            // Interval between pings
            case 't':
                timeout = atof(optarg);
                if (timeout <= 0.0f)
                {
                    printf("\nInvalid ping timeout: \"%s\"!\n", optarg);
                    return EX_USAGE;
                }
                break;
            case 'I':
                if (strlen(optarg) + 1 > sizeof(ip))
                {
                    printf("\nInvalid IP address length!\n");
                    return EX_USAGE;
                }
                strcpy(ip, optarg);
                break;
            case 'p':
                port = atoi(optarg);
                if ((port <= 0) || (port > UINT16_MAX))
                {
                    printf("\nInvalid port: \"%s\"!\n", optarg);
                    return EX_USAGE;
                }
                break;
            // Help request
            case 'h':
            // Help request
            case '?':
                puts(
                        "\nUsage:\n\tmavlink-ping [-d] [-h] [-c <count>] [-i <interval>] -I <ip> -p <port>\n"
                        "\t\t<id> <comp>\n"
                        "Options:\n\t"
                        "-d - print debug output,\n\t"
                        "-c - number of pings to send,\n\t"
                        "-i - interval between pings,\n\t"
                        "-I - UDP endpoint target IP,\n\t"
                        "-p - UDP endpoint target port,\n\t"
                        "-h - print this help.\n\n\t"
                        "<id> - MAVLink ID,\n\t"
                        "<comp> - MAVLInk component ID.\n"
                );
                return EX_USAGE;
                break;
            default:
                return EX_USAGE;
        }

    if (!ip[0])
    {
        printf("\nIP address is not presented!\n");
        return EX_USAGE;
    }

    if (!port)
    {
        printf("\nUDP port is not presented!\n");
        return EX_USAGE;
    }


    // Configuration file path (last position argument)
    if (argc - optind == 2)
    {
        target_id = atoi(argv[optind]);
        if ((target_id <= 0) || (target_id > UINT8_MAX))
        {
            printf("\nInvalid MAVLink target ID: \"%s\"!\n", argv[optind]);
            return EX_USAGE;
        }

        target_component = atoi(argv[optind + 1]);
        if ((target_component <= 0) || (target_component > UINT8_MAX))
        {
            printf("\nInvalid MAVLink target ID: \"%s\"!\n", argv[optind + 1]);
            return EX_USAGE;
        }
    }
    else
    {
        printf("\nNot all position arguments are set!\n");
        return EX_USAGE;
    }

    printf("\n");

    if (debug)
        printf("Debug mode enabled\n");

    if (debug)
        printf("UDP socket setup...\n");
    // Create UDP socket
    udp_socket_fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket_fd < 0)
    {
        printf("Error creating UDP socket: %s\n", strerror(errno));
        return EX_OSERR;
    }

    if (debug)
        printf("Network addresses setup...\n");

    // Local address
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    // Bind on all interfaces
    local_addr.sin_addr.s_addr = INADDR_ANY;
    // OS will determine local port later
    local_addr.sin_port = htons(0);

    // Remote address
    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    // Convert string to the IP address
    if (!inet_aton(ip, &remote_addr.sin_addr))
    {
        printf("Invalid IP address: \"%s\"\n", ip);
        return EX_USAGE;
    }
    remote_addr.sin_port = htons(port);

    if (debug)
        printf("UDP socket bind...\n");
    // Bind UDP socket to local address
    if (bind(udp_socket_fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) == -1)
    {
        printf("Error binding socket: %s\n", strerror(errno));
        return EX_OSERR;
    }

    if (debug)
        printf("Timer setup...\n");
    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timer_fd < 0)
    {
        printf("Error creating ping interval timer: %s\n", strerror(errno));

        close(udp_socket_fd);

        return EX_OSERR;
    }

    struct itimerspec ping_interval_spec;
    memset(&ping_interval_spec, 0, sizeof(ping_interval_spec));
    ping_interval_spec.it_value.tv_sec = (time_t)interval;
    ping_interval_spec.it_value.tv_nsec = (time_t)((interval - ping_interval_spec.it_value.tv_sec) * 1000000000.0f);

    memset(&timeout_spec, 0, sizeof(timeout_spec));
    timeout_spec.it_value.tv_sec = (time_t)timeout;
    timeout_spec.it_value.tv_nsec = (time_t)((timeout - timeout_spec.it_value.tv_sec) * 1000000000.0f);

    // Signals to block
    sigset_t mask;
    // Clear the mask
    sigemptyset(&mask);
    // Set signals to ignore
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);

    if (debug)
        printf("Signals setup...\n");
    // Original signal parameters
    sigset_t orig_mask;
    // Block signals according to mask and save previous mask
    if (sigprocmask(SIG_BLOCK, &mask, &orig_mask) < 0)
    {
        printf("Error setting new signal mask: %s\n", strerror(errno));

        close(timer_fd);
        close(udp_socket_fd);

        return EX_OSERR;
    }

    // Read fd set for select
    fd_set read_fds;
    // We need a fd with the maximum number to make a correct select request
    int fd_max = (udp_socket_fd > timer_fd) ? udp_socket_fd : timer_fd;
    // select fds number
    int select_fds_num;

    // WARNING: No SIGINT and SIGTERM from this point

    // MAVLink message buffer
    mavlink_message_t message;
    // MAVLink message parsing status
    mavlink_status_t status;

    // Data read buffer
    uint8_t read_buf[MAVLINK_MAX_PACKET_LEN];
    // Read data counter
    ssize_t data_read;

    seq = 0;
    state = IDLE;
    unsigned int ping_lost = 0;
    unsigned int ping_recieved = 0;
    double ping_rtt_sum = 0;
    double ping_rtt_min = INFINITY;
    double ping_rtt_max = 0;

    static struct timespec start_stamp;
    if (clock_gettime(CLOCK_MONOTONIC, &start_stamp) < 0)
    {
        printf("Failed to get current time: %s\n", strerror(errno));

        close(timer_fd);
        close(udp_socket_fd);

        return EX_OSERR;
    }

    if (send_ping() < 0)
    {
        close(timer_fd);
        close(udp_socket_fd);

        return EX_OSERR;
    }

    if (debug)
        printf("Main loop started\n\n");
    while (!stop_application)
    {
        // Reset fd set (select modifies this set to return the answer)
        FD_ZERO(&read_fds);
        // Set UDP socket fd
        FD_SET(udp_socket_fd, &read_fds);
        // Set timer fd
        FD_SET(timer_fd, &read_fds);

        // Wait for data at any fd and process SIGINT and SIGTERM
        select_fds_num = pselect(fd_max + 1, &read_fds, NULL, NULL, NULL, &orig_mask);
        // select returned an error
        if (select_fds_num < 0)
        {
            if (errno != EINTR)
            {
                printf("select failed: %s\n", strerror(errno));

                close(timer_fd);
                close(udp_socket_fd);

                return EX_OSERR;
            }
            else
                continue;
        }

        // New data from the UDP socket
        if (FD_ISSET(udp_socket_fd, &read_fds))
        {
            // Read data
            data_read = read(udp_socket_fd, &read_buf, sizeof(read_buf));
            if (select_fds_num < 0)
            {
                printf("Failed to read from the socket: %s\n", strerror(errno));

                close(timer_fd);
                close(udp_socket_fd);

                return EX_OSERR;
            }
            // For every byte
            for (int i = 0; i < data_read; i++)
            {
                // Parse using MAVLink
                if (mavlink_parse_char(MAVLINK_COMM_0, read_buf[i], &message, &status) &&
                        (message.msgid == MAVLINK_MSG_ID_PING) &&
                        (message.sysid == target_id) &&
                        (message.compid == target_component) &&
                        (mavlink_msg_ping_get_target_system(&message) == SOURCE_MAVLINK_ID) &&
                        (mavlink_msg_ping_get_target_component(&message) == SOURCE_MAVLINK_COMPONENT) &&
                        (mavlink_msg_ping_get_seq(&message) == seq))
                {
                    static struct timespec stamp;
                    if (clock_gettime(CLOCK_MONOTONIC, &stamp) < 0)
                    {
                        printf("Failed to get current time: %s\n", strerror(errno));

                        close(timer_fd);
                        close(udp_socket_fd);

                        return EX_OSERR;
                    }

                    double ping_rtt = (double)(stamp.tv_sec - ping_stamp.tv_sec) * 1000 +
                            (double)(stamp.tv_nsec - ping_stamp.tv_nsec) / 1000000.0;
                    ping_rtt_sum += ping_rtt;
                    if (ping_rtt > ping_rtt_max)
                        ping_rtt_max = ping_rtt;
                    if (ping_rtt < ping_rtt_min)
                        ping_rtt_min = ping_rtt;

                    printf("Ping response from %lu:%lu: seq=%u time=%.1f ms\n", target_id, target_component, seq,
                           ping_rtt);
                    if (timerfd_settime(timer_fd, 0, &ping_interval_spec, NULL) < 0)
                    {
                        printf("Failed to start timer with ping interval: %s\n", strerror(errno));

                        close(timer_fd);
                        close(udp_socket_fd);

                        return EX_OSERR;
                    }

                    ping_recieved++;
                    state = IDLE;
                }
            }
        }

        // Timeout timer has fired
        if (FD_ISSET(timer_fd, &read_fds))
        {
            // Read the data to reset timer event
            uint64_t buf;
            size_t ret = read(timer_fd, &buf, sizeof(buf));
            if (ret < 0) {
                printf("Failed to reset timeout timer event: %s\n", strerror(errno));

                close(timer_fd);
                close(udp_socket_fd);

                return EX_OSERR;
            }

            switch(state)
            {
                case IDLE:
                    break;
                case WAITING_RESPONSE:
                    ping_lost++;
                    if (debug)
                        printf("Ping response timeout\n");
                    break;
                default:
                    break;
            }

            if (count && ((seq + 1) >= count))
                break;
            seq++;
            if (send_ping() < 0)
            {
                close(timer_fd);
                close(udp_socket_fd);

                return EX_OSERR;
            }
        }
    }

    close(timer_fd);
    close(udp_socket_fd);

    static struct timespec stop_stamp;
    if (clock_gettime(CLOCK_MONOTONIC, &stop_stamp) < 0)
    {
        printf("Failed to get current time: %s\n", strerror(errno));
        return EX_OSERR;
    }

    printf("--- %lu:%lu ping statistics ---\n", target_id, target_component);
    printf("%u packets transmitted, %u received, %u%% packet loss, time %u ms\n", seq + 1, ping_recieved,
           (unsigned int)round((float)ping_lost / (float)(ping_recieved + ping_lost) * 100.0f),
           (unsigned int)round((double)(stop_stamp.tv_sec - start_stamp.tv_sec) * 1000 +
           (double)(stop_stamp.tv_nsec - start_stamp.tv_nsec) / 1000000.0));

    if (ping_recieved)
        printf("rtt min/avg/max = %.3f/%.3f/%.3f ms\n", ping_rtt_min, ping_rtt_sum / (double)ping_recieved,
                ping_rtt_max);

    return EX_OK;
}
