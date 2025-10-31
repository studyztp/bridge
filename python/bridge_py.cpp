#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/pytypes.h>

#include "../bridge.hpp"
#include <stdexcept>

namespace py = pybind11;

static py::bytes message_payload_to_pybytes(const Message &m) {
    if (m.data.empty()) return py::bytes();
    return py::bytes(reinterpret_cast<const char*>(m.data.data()), m.data.size());
}

PYBIND11_MODULE(_bridge, m) {
    m.doc() = "Python bindings for bridge library (Unix-domain socket helpers)";

    // COMMAND enum
    py::enum_<COMMAND>(m, "COMMAND")
        .value("INVALID", COMMAND::INVALID)
        .value("INITIALIZE", COMMAND::INITIALIZE)
        .value("ASK_FOR_COMPUTE", COMMAND::ASK_FOR_COMPUTE)
        .value("COMPUTE_FINISH", COMMAND::COMPUTE_FINISH)
        .value("COMPUTE_IN_PROGRESS", COMMAND::COMPUTE_IN_PROGRESS)
        .value("COMPUTE_DATA_READY", COMMAND::COMPUTE_DATA_READY)
        .value("ASK_FOR_SCHEDULE_STOP", COMMAND::ASK_FOR_SCHEDULE_STOP)
        .value("WAIT_FOR_ROBOT_SIGNAL", COMMAND::WAIT_FOR_ROBOT_SIGNAL)
        .export_values();

    // Message class
    py::class_<Message>(m, "Message")
        .def(py::init<>())
        .def_readwrite("command", &Message::command)
        .def_property("data",
            [](const Message &msg){
                return message_payload_to_pybytes(msg);
            },
            [](Message &msg, py::bytes b){
                std::string s = static_cast<std::string>(b);
                msg.data.assign(reinterpret_cast<const uint8_t*>(s.data()),
                                reinterpret_cast<const uint8_t*>(s.data()) + s.size());
            }
        );

    // Server-side: simulation_connections helpers (get/set/copy)
    m.def("get_simulation_connections", [](){
        py::dict d;
        for (const auto &kv : simulation_connections) {
            d[py::str(kv.first)] = py::str(kv.second);
        }
        return d;
    });

    m.def("set_simulation_connections", [](py::dict d){
        simulation_connections.clear();
        for (auto item : d) {
            std::string k = py::str(item.first);
            std::string v = py::str(item.second);
            simulation_connections[k] = v;
        }
    }, py::arg("mapping"));

    m.def("sim_set", [](const std::string &k, const std::string &v){
        simulation_connections[k] = v;
    }, py::arg("source"), py::arg("target"));

    m.def("sim_clear", [](){ simulation_connections.clear(); });

    // setup_bridge_client: returns fd
    m.def("setup_bridge_client", [](const std::string &client_name) -> int {
        int fd = setup_bridge_client(client_name);
        if (fd < 0) throw std::runtime_error("setup_bridge_client failed");
        return fd;
    }, py::arg("client_name"));

    // client_send_and_wait: convenience wrapper that builds Message, serializes it,
    // calls bridge_client_send_and_wait_response and returns a Message instance.
    m.def("client_send_and_wait", [](int client_fd, int command, py::bytes payload, int timeout_ms){
        Message req;
        req.command = static_cast<COMMAND>(command);
        std::string s = static_cast<std::string>(payload);
        req.data.assign(reinterpret_cast<const uint8_t*>(s.data()),
                        reinterpret_cast<const uint8_t*>(s.data()) + s.size());

        size_t out_len = 0;
        uint8_t* out_buf = nullptr;
        convert_message_to_data(req, out_len, out_buf);
        if (!out_buf) throw std::runtime_error("convert_message_to_data failed");

        Message resp;
        try {
            // release the GIL while waiting for the response (blocking C++ I/O)
            py::gil_scoped_release release;
            resp = bridge_client_send_and_wait_response(client_fd, out_buf, out_len, timeout_ms);
        } catch (...) {
            delete[] out_buf;
            throw;
        }
        delete[] out_buf;

        // return a new Message (pybind will convert)
        return resp;
    }, py::arg("client_fd"), py::arg("command"), py::arg("payload") = py::bytes(""), py::arg("timeout_ms") = -1);

    // get_peer_pid
    m.def("get_peer_pid", [](int fd) -> int {
        pid_t pid = get_peer_pid(fd);
        if (pid < 0) throw std::runtime_error("get_peer_pid failed");
        return static_cast<int>(pid);
    }, py::arg("fd"));

    // interrupt_client
    m.def("interrupt_client", [](const std::string &name){
        interrupt_client(name);
    }, py::arg("name"));

    // utility: close fd
    m.def("close_fd", [](int fd){ if (fd >= 0) ::close(fd); }, py::arg("fd"));

    // Expose server socket setup and run loop (advanced use)
    m.def("setup_bridge_server_socket", []() -> int { return setup_bridge_server_socket(); });
    m.def("run_bridge_server_loop", [](int listen_fd){
        py::gil_scoped_release release;
        run_bridge_server_loop(listen_fd);
    }, py::arg("listen_fd"));
}
