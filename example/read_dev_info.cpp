// compile: g++ -o read_dev_info example/read_dev_info.cpp -std=c++14 -Isrc -Ldist/release/lib/x64 -lwarble
#include "warble/warble.h"

#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <queue>
#include <stdexcept>
#include <string>

using namespace std;

static function<void(void)> read_completed;
static string read_char_values(WarbleGatt* gatt, const char* service, const char* uuid) {
    promise<WarbleGattChar*> find_char;
    warble_gatt_find_characteristic_async(gatt,service, uuid, &find_char, [](void* context, WarbleGattChar* gatt_char, const char* error) {
        auto param = (promise<WarbleGattChar*>*) context;

        if (gatt_char == nullptr) {
            param->set_exception(make_exception_ptr(runtime_error(error)));
        } else {
            param->set_value(gatt_char);
        }
    });

    promise<string> read_value;
    warble_gattchar_read_async(find_char.get_future().get(), &read_value, [](void* context, WarbleGattChar* caller, const uint8_t* value, uint8_t length, const char* error) {
        auto param = (promise<string>*) context;
        if (error != nullptr) {
            param->set_exception(make_exception_ptr(runtime_error(error)));
        } else {
            param->set_value(string(value, value + length));
        }
    });

    return read_value.get_future().get();
}

static condition_variable cv;

int main(int argc, char** argv) {
    auto gatt = warble_gatt_create(argv[1]);

    promise<void> connect;
    warble_gatt_connect_async(gatt, &connect, [](void* context, WarbleGatt* caller, const char* error) {
        auto param = (promise<void>*) context;
        if (error != nullptr) {
            param->set_exception(make_exception_ptr(runtime_error(error)));
        } else {
            param->set_value();
        }
    });
    connect.get_future().get();

    queue<const char*> uuids;
    uuids.push("00002a26-0000-1000-8000-00805f9b34fb");
    uuids.push("00002a24-0000-1000-8000-00805f9b34fb");
    uuids.push("00002a27-0000-1000-8000-00805f9b34fb");
    uuids.push("00002a29-0000-1000-8000-00805f9b34fb");
    uuids.push("00002a25-0000-1000-8000-00805f9b34fb");

    while (!uuids.empty()) {
        try {
            cout << uuids.front() << ": " << read_char_values(gatt, "0000180a-0000-1000-8000-00805f9b34fb", uuids.front()) << endl;
        } catch (const exception& e) {
            cout << "failed to read '" << uuids.front() << "': " << e.what() << endl;
        }
        uuids.pop();
    }

    return 0;
}