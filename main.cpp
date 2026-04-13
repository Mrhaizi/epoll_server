#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cerrno>
#include <fcntl.h>
#include <arpa/inet.h>

const static int listen_port = 7070;

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        close(listen_fd);
        return 0;
    }
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsocketopt");
        close(listen_fd);
        return 0;
    }

    if (set_nonblocking(listen_fd) < 0) {
        perror("set_nonblocking listen_fd");
        close(listen_fd);
        return 0;
    }
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(listen_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listen_fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 0;
    }
    if (listen(listen_fd, 128)) {
        perror("listen");
        close(listen_fd);
        return 0;
    }

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        close(epoll_fd);
        return 0;
    }

    epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev)) {
        perror("epoll_ctl");
        close(listen_fd);
        close(epoll_fd);
        return 0;
    }
    printf("server liseten 0.0.0.0:%d\n", listen_port);
    epoll_event events[1024];
    while (true) {
        int n = epoll_wait(epoll_fd, events , 1024, 60);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("epoll_wait");
            break;
        }
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            uint32_t revents  = events[i].events;
            if (fd == listen_fd) {
                // TODO : 处理新到来的连接
                while (true) {
                    sockaddr_in client_addr {};
                    socklen_t client_len = sizeof(client_addr);

                    // 拿到连接
                    int conn_fd = accept(fd, (sockaddr* )&client_addr , &client_len);  
                    if (conn_fd < 0) {
                        // 暂时没有可读的数据
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        }
                        perror("accept");
                        break;
                    }

                    // 设置非阻塞
                    if (set_nonblocking(conn_fd) < 0) {
                        perror("set_nonblocking conn_fd");
                        close(conn_fd);
                        continue;
                    }

                    // 设置epoll_event
                    epoll_event client_ev {};
                    client_ev.events = EPOLLIN | EPOLLRDHUP; // 连接上有数据或者对端关闭/半关闭通知我
                    client_ev.data.fd = conn_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &client_ev) < 0) {
                        perror("epoll_ctl");
                        close(conn_fd);
                        continue;
                    }
                    char ip[INET_ADDRSTRLEN] = {0};
                    inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
                    std::cout << "new connection: fd=" << conn_fd
                              << " ip=" << ip
                              << " port=" << ntohs(client_addr.sin_port)
                              << std::endl;
                }
            } else {
                // 如果当前fd已经挂了，出错了，对端关闭了就直接删除并且释放当前conn_fd;
                if (revents & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
                    std::cout << "close conn fd:" << fd << "\n";
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                    continue;
                }
                // 如果当前fd的event是EPOLLIN
                if (revents & EPOLLIN) {
                    char buf[4096];
                    // 处理发过来的数据
                    while (true) {
                        ssize_t ret = read(fd, buf, sizeof(buf));
                        if (ret > 0) {
                            ssize_t sent = 0;
                            while (sent < ret) {
                                ssize_t w = write (fd, buf + sent, ret - sent);
                                if (w > 0) {
                                    sent += w;
                                } else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                                    break;
                                } else {
                                    perror("write");
                                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                                    close(fd);
                                    fd = -1;
                                    break;
                                }
                            }
                            if (fd == -1)  {
                                break;
                            }
                        } else if (ret == 0) {
                            std::cout << "conn disconnect: fd=" << fd << "\n";
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                            close(fd);
                            break;
                        } else {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                break;
                            }
                            perror("read");
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                            close(fd);
                            break;
                        }
                    }
                }

            }
        }
    }
    close(epoll_fd);
    close(listen_fd);
    return 0;
}
