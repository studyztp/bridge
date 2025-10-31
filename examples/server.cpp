#include "bridge.hpp"

int main() {
    simulation_connections["A"] = "B";
    simulation_connections["B"] = "A";
    // This will block and run the server loop
    setup_bridge_server();
    return 0;
}
