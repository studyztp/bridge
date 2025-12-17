// Clean single-file pybind11 wrapper for bridge
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/pytypes.h>

#include "../bridge.hpp"
#include <stdexcept>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace py = pybind11;

static py::bytes message_payload_to_pybytes(const Message &m) {
    if (m.data.empty()) return py::bytes();
    return py::bytes(reinterpret_cast<const char*>(m.data.data()), m.data.size());
}

PYBIND11_MODULE(_bridge, m) {
    m.doc() = "Python bindings for bridge library (Unix-domain socket helpers)";

    // COMMAND enum (match values defined in bridge.hpp)
    py::enum_<COMMAND>(m, "COMMAND")
        .value("INVALID", COMMAND::INVALID)
        .value("INITIALIZE_SERVER", COMMAND::INITIALIZE_SERVER)
        .value("INITIALIZE_CLIENT", COMMAND::INITIALIZE_CLIENT)
        .value("COMPUTE_REQUEST", COMMAND::COMPUTE_REQUEST)
        .value("COMPUTE_RESPONSE", COMMAND::COMPUTE_RESPONSE)
        .value("SETUP_TIMESTEP", COMMAND::SETUP_TIMESTEP)
        .export_values();

    // Message class
    py::class_<Message>(m, "Message")
        .def(py::init<>())
        .def_readwrite("command", &Message::command)
        .def_property("data",
            [](const Message &msg){ return message_payload_to_pybytes(msg); },
            [](Message &msg, py::bytes b){
                std::string s = static_cast<std::string>(b);
                msg.data.assign(reinterpret_cast<const uint8_t*>(s.data()),
                                reinterpret_cast<const uint8_t*>(s.data()) + s.size());
            }
        );

    // Helper server socket
    m.def("bridge_setup_helper_server_socket", [](){
        int fd = bridge_setup_helper_server_socket();
        if (fd < 0) throw std::runtime_error("bridge_setup_helper_server_socket failed");
        return fd;
    });

    m.def("bridge_close_helper_server_socket", [](int listen_fd){
        return bridge_close_helper_server_socket(listen_fd);
    }, py::arg("listen_fd"));

    m.def("bridge_helper_server_loop", [](int listen_fd, std::unordered_map<std::string,std::string> mapping){
        // release GIL as this is a blocking loop
        py::gil_scoped_release release;
        return bridge_helper_server_loop(listen_fd, mapping);
    }, py::arg("listen_fd"), py::arg("client_to_server_match"));

    // Server setup: returns tuple (client_pid, listen_fd)
    m.def("bridge_setup_server", [](const std::string &server_name){
        pid_t client_pid = -1;
        int socket_fd = -1;
        if (bridge_setup_server(server_name, client_pid, socket_fd) < 0) {
            throw std::runtime_error("bridge_setup_server failed");
        }
        return py::make_tuple((int)client_pid, socket_fd);
    }, py::arg("server_name"));

    // Client setup: returns tuple (server_pid, socket_fd)
    m.def("bridge_setup_client", [](const std::string &client_name){
        pid_t server_pid = -1;
        int socket_fd = -1;
        if (bridge_setup_client(client_name, server_pid, socket_fd) < 0) {
            throw std::runtime_error("bridge_setup_client failed");
        }
        return py::make_tuple((int)server_pid, socket_fd);
    }, py::arg("client_name"));

    // send and wait for response
    m.def("bridge_send_and_wait_for_response", [](int socket_fd, const Message &msg, int timeout_ms){
        Message resp;
        if (bridge_send_and_wait_for_response(socket_fd, msg, resp, timeout_ms) < 0) {
            throw std::runtime_error("bridge_send_and_wait_for_response failed");
        }
        return resp;
    }, py::arg("socket_fd"), py::arg("msg"), py::arg("timeout_ms") = -1);

    m.def("bridge_wait_for_message", [](int socket_fd, int timeout_ms){
        // release GIL while blocking read
        py::gil_scoped_release release;
        return bridge_wait_for_message(socket_fd, timeout_ms);
    }, py::arg("socket_fd"), py::arg("timeout_ms") = -1);

    m.def("bridge_send_message", [](int socket_fd, const Message &msg){
        if (bridge_send_message(socket_fd, msg) < 0) throw std::runtime_error("bridge_send_message failed");
        return 0;
    }, py::arg("socket_fd"), py::arg("msg"));

    m.def("bridge_send_interrupt", [](int pid){
        if (bridge_send_interrupt((pid_t)pid) < 0) throw std::runtime_error("bridge_send_interrupt failed");
        return 0;
    }, py::arg("pid"));

    m.def("get_fd_pid", [](int fd)->int{
        pid_t p = get_fd_pid(fd);
        if (p < 0) throw std::runtime_error("get_fd_pid failed");
        return (int)p;
    }, py::arg("fd"));
}
