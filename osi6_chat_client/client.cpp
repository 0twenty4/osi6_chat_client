#include <locale.h>
#include <string>
#include <iostream>
#include <thread>
#include <deque>
#include <mutex>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ncursesw/ncurses.h>
#include <boost/locale.hpp>
#include <optional>

enum Status { ERROR, SUCCESS, EXIT };

using Window = std::unique_ptr<WINDOW, decltype(&delwin)>;

struct Chat {
    Window window{ nullptr, &delwin };
    std::deque<std::wstring> client_messages_buf{};
    int window_scroll_offset = 0;
} chat;

struct Client {
    Window input_window{ nullptr, &delwin };
    int socket = 0;
};

struct Client_input_result {
    std::wstring message_utf16;
    bool is_submitted = false;
    bool is_scroll_up = false;
    bool is_scoll_down = false;
};

std::optional<Client> client;

std::pair<int, int> get_cursor_position(const WINDOW* window) {
    int x, y;
    getyx(window, y, x);
    return std::pair<int, int>(x, y);
}

int send_message(const std::string& message) {
    int ret_val = send(client->socket, message.data(), message.size(), 0);
    if (ret_val < 0) {
        perror("send");
        close(client->socket);
        return ERROR;
    }
    return SUCCESS;
}

Client_input_result client_input(const std::wstring& not_submitted_message = L"") {
    Client_input_result client_input_result;
    client_input_result.message_utf16 = not_submitted_message;

    wint_t key;
    int key_type;

    const int message_start_x = 3;
    int message_end_x = message_start_x + client_input_result.message_utf16.size();

    const int input_start_x = 1;
    const int input_start_y = 1;

    mvwprintw(client->input_window.get(), input_start_y, input_start_x, "> %ls",
        not_submitted_message.c_str());
    wrefresh(client->input_window.get());

    while (true) {
        key_type = wget_wch(client->input_window.get(), &key);
        auto [cursor_x, cursor_y] = get_cursor_position(client->input_window.get());

        if (key_type == KEY_CODE_YES) {
            if (key == KEY_BACKSPACE) {
                if (client_input_result.message_utf16.empty())
                    continue;

                client_input_result.message_utf16.erase(cursor_x - message_start_x - 1, 1);
                message_end_x--;

                werase(client->input_window.get());
                mvwprintw(
                    client->input_window.get(), input_start_y, input_start_x, "> %ls",
                    client_input_result.message_utf16.data());
                wrefresh(client->input_window.get());
                wmove(client->input_window.get(), cursor_y, cursor_x - 1);
            }
            else if (key == KEY_LEFT) {
                if (cursor_x == message_start_x)
                    continue;

                wmove(client->input_window.get(), cursor_y, cursor_x - 1);
            }
            else if (key == KEY_RIGHT) {
                if (cursor_x == message_start_x + static_cast<int>(client_input_result.message_utf16.size()))
                    continue;

                wmove(client->input_window.get(), cursor_y, cursor_x + 1);
            }
            else if (key == KEY_DOWN) {
                client_input_result.is_scoll_down = true;
                return client_input_result;
            }
            else if (key == KEY_UP) {
                client_input_result.is_scroll_up = true;
                return client_input_result;
            }
        }
        else if (key_type == OK) {

            if (key == '\n') {
                client_input_result.is_submitted = true;

                werase(client->input_window.get());
                mvwprintw(client->input_window.get(), input_start_y, input_start_x, "> ");
                wrefresh(client->input_window.get());

                return client_input_result;
            }

            if (cursor_x == message_end_x)
                client_input_result.message_utf16.push_back(static_cast<wchar_t>(key));
            else {
                const auto cursor_message_position =
                    client_input_result.message_utf16.begin() + cursor_x - message_start_x;
                client_input_result.message_utf16.insert(cursor_message_position, static_cast<wchar_t>(key));
            }

            message_end_x++;

            werase(client->input_window.get());
            mvwprintw(client->input_window.get(), input_start_y, input_start_x, "> %ls",
                client_input_result.message_utf16.data());
            wrefresh(client->input_window.get());
            wmove(client->input_window.get(), cursor_y, cursor_x + 1);
        }
    }
}


int connect_server() {
    const std::string enter_ip_port_message = "Enter chat server IP and port seperated by \':\'\n";
    mvwprintw(chat.window.get(), 0, 0, "%s", enter_ip_port_message.data());
    wrefresh(chat.window.get());

    Client_input_result client_input_result;
    while (!client_input_result.is_submitted)
        client_input_result = client_input();

    const int server_ip_port_separator_position = client_input_result.message_utf16.find(':');
    const std::wstring server_ip_utf16 =
        client_input_result.message_utf16.substr(0, server_ip_port_separator_position);
    const int server_port =
        std::stoi(client_input_result.message_utf16.substr(server_ip_port_separator_position + 1).data());

    sockaddr_in server_sockaddr{};
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_port = htons(server_port);
    const std::string server_ip_utf8 = boost::locale::conv::utf_to_utf<char, wchar_t>(server_ip_utf16.data());
    inet_pton(AF_INET, server_ip_utf8.data(), &server_sockaddr.sin_addr);

    if (connect(client->socket, (sockaddr*)&server_sockaddr, sizeof(server_sockaddr)) < 0) {
        perror("connect");
        close(client->socket);
        return ERROR;
    }
    return SUCCESS;
}

std::optional<Client> make_client() {
    Client client;

    client.socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client.socket <= 0) {
        perror("socket");
        return std::nullopt;
    }

    return client;
}

int register_client() {
    std::string client_name;
    const int client_name_max_len = 20;

    client_name.resize(client_name_max_len + 1);

    const std::string enter_name_message = "Enter your name (20 symbols):\n";
    mvwprintw(chat.window.get(), 1, 0, "%s", enter_name_message.data());
    wrefresh(chat.window.get());

    Client_input_result client_input_result;
    while (!client_input_result.is_submitted)
        client_input_result = client_input();

    const std::string client_message_utf8 =
        boost::locale::conv::utf_to_utf<char, wchar_t>(client_input_result.message_utf16);
    
    return send_message("/register " + client_message_utf8);
}

int process_client_command(const std::string& command) {
    if (command == "/exit") {
        send_message(command);
        return EXIT;
    }
    return SUCCESS;
}

int process_client_message(const std::string& client_message) {
    if (!client_message.empty() and client_message[0] == '/')
        return process_client_command(client_message);
    else 
        return send_message(client_message);
}

void draw_chat_window() {
    werase(chat.window.get());

    for (int message_index = 0; message_index < static_cast<int>(chat.client_messages_buf.size()); message_index++) {

        std::wstring print_chat_message = chat.client_messages_buf[message_index].substr(
            chat.client_messages_buf[message_index].find_first_of(' ')+1);

        wprintw(chat.window.get(), "%ls\n", print_chat_message.c_str());
    }

    wrefresh(chat.window.get());
}

int process_server_command(const std::string& command, const std::string& arguments) {
    if (command == "/scroll_up") {

        if (!chat.client_messages_buf.empty())
            chat.client_messages_buf.pop_back();

        const std::wstring scroll_up_chat_message_utf16 =
            boost::locale::conv::utf_to_utf<wchar_t, char>(arguments);

        chat.client_messages_buf.push_front(scroll_up_chat_message_utf16);
    }
    else if (command == "/scroll_down") {

        if (!chat.client_messages_buf.empty())
            chat.client_messages_buf.pop_front();

        const std::wstring scroll_down_chat_message_utf16 =
            boost::locale::conv::utf_to_utf<wchar_t, char>(arguments);

        chat.client_messages_buf.push_back(scroll_down_chat_message_utf16);
    }

    return SUCCESS;
}

int proccess_server_message(const std::string& server_message) {
    if (server_message.at(0) == '/') {
        std::string command = server_message.substr(0, server_message.find(' '));
        std::string arguments = server_message.substr(server_message.find(' ') + 1);

        process_server_command(command, arguments);

    }
    else {
        if (chat.client_messages_buf.size() == 25)
            chat.client_messages_buf.pop_front();

        const std::wstring chat_message = 
            boost::locale::conv::utf_to_utf<wchar_t, char>(server_message);

        chat.client_messages_buf.push_back(chat_message);
    }

    return SUCCESS;
}

int receiving_server_messages() {
    std::string server_message;

    while (true) {
        server_message.clear();
        server_message.resize(1024);

        int ret_val = recv(client->socket, server_message.data(), server_message.size(), 0);
        if (ret_val < 0) {
            perror("recv");
            close(client->socket);
            return ERROR;
        }
        server_message.resize(ret_val);

        proccess_server_message(server_message);
        
        draw_chat_window();
    }
}

int proccess_client() {
    Client_input_result client_input_result;

    while (true) {
        client_input_result = client_input();

        if (client_input_result.is_submitted) {
            const std::string client_message_utf8 =
                boost::locale::conv::utf_to_utf<char, wchar_t>(client_input_result.message_utf16);
            send_message(client_message_utf8);
            
            if (client_message_utf8 == "/exit")
                return EXIT;
        }
        else if (client_input_result.is_scoll_down) {
            std::string newest_client_printed_chat_message_index =
                boost::locale::conv::utf_to_utf<char, wchar_t>(
                    chat.client_messages_buf.back().substr(0, chat.client_messages_buf.back().find(' ')));

            send_message("/scroll_down " + newest_client_printed_chat_message_index);
        }
        else if (client_input_result.is_scroll_up) {
            std::string oldest_client_printed_chat_message_index =
                boost::locale::conv::utf_to_utf<char, wchar_t>(
                    chat.client_messages_buf.front().substr(0, chat.client_messages_buf.front().find(' ')));

            send_message("/scroll_up " + oldest_client_printed_chat_message_index);
        }
    }
}

void make_gui() {
    initscr();
    cbreak();
    noecho();
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    chat.window.reset(newwin(max_y - 2, max_x, 0, 0));
    scrollok(chat.window.get(), TRUE);
    
    client->input_window.reset(newwin(2, max_x, max_y - 2, 0));
    keypad(client->input_window.get(), TRUE);
}

int main() {
    setlocale(LC_ALL, "");

    client = make_client();

    if (!client.has_value())
        return ERROR;

    make_gui();

    if (connect_server() == ERROR)
        return ERROR;

    std::thread receiving_server_messages_thread(receiving_server_messages);
    receiving_server_messages_thread.detach();

    if (register_client() == ERROR)
        return ERROR;

    proccess_client();

    endwin();
    return SUCCESS;
}
