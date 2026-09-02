#include <modbus.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    const int port = argc > 2 ? std::atoi(argv[2]) : 502;
    const int unit = argc > 3 ? std::atoi(argv[3]) : 0xFF;
    const int iterations = argc > 4 ? std::atoi(argv[4]) : 20;

    const bool use_tcp_pi = argc > 5 && std::string(argv[5]) == "tcp_pi";
    const std::string service = std::to_string(port);
    modbus_t* context = use_tcp_pi
        ? modbus_new_tcp_pi(host.c_str(), service.c_str())
        : modbus_new_tcp(host.c_str(), port);
    if (context == nullptr) {
        std::cerr << "Unable to create Modbus TCP context.\n";
        return 1;
    }

    modbus_set_debug(context, TRUE);
    modbus_set_slave(context, unit);
    modbus_set_response_timeout(context, 0, 200000);
    if (modbus_connect(context) == -1) {
        std::cerr << "Connect failed: " << modbus_strerror(errno) << '\n';
        modbus_free(context);
        return 1;
    }

    std::vector<uint16_t> registers(4);
    int failures = 0;
    for (int iteration = 0; iteration < iterations; ++iteration) {
        const int result = modbus_read_input_registers(
            context, 0, static_cast<int>(registers.size()), registers.data());
        if (result == -1) {
            ++failures;
            std::cerr << "Iteration " << iteration << " failed (errno=" << errno
                      << "): " << modbus_strerror(errno) << '\n';
        } else {
            std::cout << "Iteration " << iteration << " read " << result << " register(s):";
            for (const auto value : registers) std::cout << ' ' << value;
            std::cout << '\n';
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    modbus_close(context);
    modbus_free(context);
    return failures == 0 ? 0 : 2;
}
