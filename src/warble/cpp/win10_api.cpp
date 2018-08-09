/**
 * @copyright MbientLab License
 */
#ifdef API_WIN10

#include "error_messages.h"
#include "gatt_def.h"
#include "gattchar_def.h"

#include <cstring>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <Windows.Devices.Bluetooth.h>

using namespace std;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Foundation;
using namespace Windows::Security::Cryptography;
using namespace Platform;

#define CHECK_TASK_ERROR(f)\
then([f](task<void> previous) {\
    try {\
        previous.wait();\
    } catch (const exception& e) {\
        f(e.what());\
    } catch (Exception^ e) {\
        wstringstream stream;\
        stream << e->Message->Data() << " (HRESULT = " << e->HResult << ")";\
\
        wstring wide(stream.str());\
        string narrow(wide.begin(), wide.end());\
\
        f(narrow.data());\
    }\
})

struct WarbleGattChar_Win10 : public WarbleGattChar {
    WarbleGattChar_Win10(WarbleGatt* owner, GattCharacteristic^ characteristic);

    virtual ~WarbleGattChar_Win10();

    virtual void write_async(const uint8_t* value, uint8_t len, void* context, FnVoid_VoidP_WarbleGattCharP_CharP handler);
    virtual void write_without_resp_async(const uint8_t* value, uint8_t len, void* context, FnVoid_VoidP_WarbleGattCharP_CharP handler);

    virtual void read_async(void* context, FnVoid_VoidP_WarbleGattCharP_UbyteP_Ubyte_CharP handler);

    virtual void enable_notifications_async(void* context, FnVoid_VoidP_WarbleGattCharP_CharP handler);
    virtual void disable_notifications_async(void* context, FnVoid_VoidP_WarbleGattCharP_CharP handler);
    virtual void on_notification_received(void* context, FnVoid_VoidP_WarbleGattCharP_UbyteP_Ubyte handler);

    virtual const char* get_uuid() const;
    virtual WarbleGatt* get_gatt() const;
private:
    inline void write_inner_async(GattWriteOption option, const uint8_t* value, uint8_t len, void* context, FnVoid_VoidP_WarbleGattCharP_CharP handler) {
        Array<byte>^ wrapper = ref new Array<byte>(len);
        for (uint8_t i = 0; i < len; i++) {
            wrapper[i] = value[i];
        }

        auto partial = bind(handler, context, this, placeholders::_1);
        auto task = characteristic->WriteValueAsync(CryptographicBuffer::CreateFromByteArray(wrapper), option);

        task->Completed = ref new AsyncOperationCompletedHandler<GattCommunicationStatus>([partial](IAsyncOperation<GattCommunicationStatus>^ result, Windows::Foundation::AsyncStatus status) {
            switch (status) {
            case Windows::Foundation::AsyncStatus::Completed:
                partial(nullptr);
                break;
            case Windows::Foundation::AsyncStatus::Canceled:
                partial("Gatt characteristic write cancelled");
                break;
            case Windows::Foundation::AsyncStatus::Error:
                stringstream buffer;
                buffer << "Failed to write gatt characteristic value (HRESULT = " << result->ErrorCode.Value << ")";

                partial(buffer.str().c_str());
                break;
            }
        });
    }

    inline void edit_notifiation_status_async(GattClientCharacteristicConfigurationDescriptorValue descriptorValue, void* context, FnVoid_VoidP_WarbleGattCharP_CharP handler) {
        auto partial = bind(handler, context, this, placeholders::_1);

        auto task = characteristic->WriteClientCharacteristicConfigurationDescriptorAsync(descriptorValue);
        task->Completed = ref new AsyncOperationCompletedHandler<GattCommunicationStatus>([partial, descriptorValue](IAsyncOperation<GattCommunicationStatus>^ result, Windows::Foundation::AsyncStatus status) {
            switch (status) {
            case Windows::Foundation::AsyncStatus::Completed:
                partial(nullptr);
                break;
            case Windows::Foundation::AsyncStatus::Canceled: {
                stringstream buffer;
                buffer << "Edit characteristic configuration (" << static_cast<int>(descriptorValue) << ") cancelled";

                partial(buffer.str().c_str());
                break;
            }
            case Windows::Foundation::AsyncStatus::Error:
                stringstream buffer;
                buffer << "Failed to edit characteristic configuration (" << static_cast<int>(descriptorValue) << ") failed (HRESULT = " << result->ErrorCode.Value << ")";

                partial(buffer.str().c_str());
                break;
            }
        });
    }

    WarbleGatt* owner;
    GattCharacteristic^ characteristic;
    Windows::Foundation::EventRegistrationToken cookie;
    string uuid_str;
};

struct Hasher {
    size_t operator() (Guid key) const {
        return key.GetHashCode();
    }
};
struct EqualFn {
    bool operator() (Guid t1, Guid t2) const {
        return t1.Equals(t2);
    }
};

struct WarbleGatt_Win10 : public WarbleGatt {
    WarbleGatt_Win10(const char* mac, BluetoothAddressType addr_type);
    virtual ~WarbleGatt_Win10();

    virtual void connect_async(void* context, FnVoid_VoidP_WarbleGattP_CharP handler);
    virtual void disconnect();
    virtual void on_disconnect(void* context, FnVoid_VoidP_WarbleGattP_Int handler);
    virtual bool is_connected() const;

    virtual WarbleGattChar* find_characteristic(const string& uuid) const;
    virtual void find_characteristic_async(const char* service, const char* uuid, void* context, void(*handler)(void*, WarbleGatt*, WarbleGattChar*));
    virtual bool service_exists(const string& uuid) const;

private:
    void cleanup(bool dispose = true);

    void device_discover_completed(void* context, FnVoid_VoidP_WarbleGattP_CharP handler);
    void discover_characteristics(GattDeviceServicesResult^ result, unsigned int i, void* context, FnVoid_VoidP_WarbleGattP_CharP handler);
    inline void connect_error(void* context, FnVoid_VoidP_WarbleGattP_CharP handler, const char* msg) {
        cleanup();
        handler(context, this, msg);
    }

    string mac;

    void *on_disconnect_context;
    FnVoid_VoidP_WarbleGattP_Int on_disconnect_handler;

    BluetoothLEDevice^ device;
    BluetoothAddressType addr_type;
    Windows::Foundation::EventRegistrationToken cookie;
    unordered_map<Guid, WarbleGattChar_Win10*, Hasher, EqualFn> characteristics;
    unordered_map<Guid, GattDeviceService^, Hasher, EqualFn> services;
};

WarbleGatt* warblegatt_create(int32_t nopts, const WarbleOption* opts) {
    const char* mac = nullptr;
    BluetoothAddressType addr_type = BluetoothAddressType::Random;
    unordered_map<string, function<void(const char*)>> arg_processors = {
        { "mac", [&mac](const char* value) {mac = value; } },
        { "address-type", [&addr_type](const char* value) {
            if (!strcmp(value, "public")) {
                addr_type = BluetoothAddressType::Public;
            } else if (!strcmp(value, "unspecified")) {
                addr_type = BluetoothAddressType::Unspecified;
            } else if (strcmp(value, "random")) {
                throw runtime_error("invalid value for \'address-type\' option (win10 api): one of [public, random, unspecified]");
            }
        }}
    };

    for (int i = 0; i < nopts; i++) {
        auto it = arg_processors.find(opts[i].key);
        if (it == arg_processors.end()) {
            throw runtime_error(string("invalid gatt option '") + opts[i].key + "'");
        }
        (it->second)(opts[i].value);
    }
    if (mac == nullptr) {
        throw runtime_error("required option 'mac' was not set");
    }

    return new WarbleGatt_Win10(mac, addr_type);
}

WarbleGatt_Win10::WarbleGatt_Win10(const char* mac, BluetoothAddressType addr_type) : mac(mac), device(nullptr), on_disconnect_context(nullptr), on_disconnect_handler(nullptr), addr_type(addr_type) {
}

WarbleGatt_Win10::~WarbleGatt_Win10() {
    cleanup();
}

void WarbleGatt_Win10::device_discover_completed(void* context, FnVoid_VoidP_WarbleGattP_CharP handler) {
    auto task = device->GetGattServicesAsync(BluetoothCacheMode::Uncached);
    task->Completed = ref new AsyncOperationCompletedHandler<GattDeviceServicesResult^>([this, context, handler](IAsyncOperation<GattDeviceServicesResult^>^ info, Windows::Foundation::AsyncStatus status) {
        switch (status) {
        case Windows::Foundation::AsyncStatus::Completed:
            discover_characteristics(info->GetResults(), 0, context, handler);
            break;
        case Windows::Foundation::AsyncStatus::Canceled:
            connect_error(context, handler, "Gatt service discovery cancelled");
            break;
        case Windows::Foundation::AsyncStatus::Error:
            stringstream buffer;
            buffer << "Failed to discover gatt services (status = " << info->ErrorCode.Value << ")";

            connect_error(context, handler, buffer.str().c_str());
            break;
        }
    });
}

void WarbleGatt_Win10::discover_characteristics(GattDeviceServicesResult^ result, unsigned int i, void* context, FnVoid_VoidP_WarbleGattP_CharP handler) {
    if (result->Services->Size <= i) {
        handler(context, this, nullptr);
        return;
    }

    auto service = result->Services->GetAt(i);
    services.emplace(service->Uuid, service);

    auto task = service->GetCharacteristicsAsync();
    task->Completed = ref new AsyncOperationCompletedHandler<GattCharacteristicsResult^>([this, i, result, context, handler](IAsyncOperation<GattCharacteristicsResult^>^ info, Windows::Foundation::AsyncStatus status) {
        switch (status) {
        case Windows::Foundation::AsyncStatus::Completed: {
            auto characteristics = info->GetResults()->Characteristics;

            for (unsigned int j = 0; j < characteristics->Size; j++) {
                this->characteristics.emplace(characteristics->GetAt(j)->Uuid, new WarbleGattChar_Win10(this, characteristics->GetAt(j)));
            }

            discover_characteristics(result, i + 1, context, handler);
            break;
        }
        case Windows::Foundation::AsyncStatus::Canceled:
            connect_error(context, handler, "Gatt characteristic discovery cancelled");
            break;
        case Windows::Foundation::AsyncStatus::Error:
            stringstream buffer;
            buffer << "Failed to discover gatt characteristics (status = " << info->ErrorCode.Value << ")";

            connect_error(context, handler, buffer.str().c_str());
            break;
        }
    });
}

void WarbleGatt_Win10::connect_async(void* context, FnVoid_VoidP_WarbleGattP_CharP handler) {
    if (device != nullptr) {
        device_discover_completed(context, handler);
    } else {
        string mac_copy(mac);
        mac_copy.erase(2, 1);
        mac_copy.erase(4, 1);
        mac_copy.erase(6, 1);
        mac_copy.erase(8, 1);
        mac_copy.erase(10, 1);

        size_t temp;
        uint64_t mac_ulong = stoull(mac_copy.c_str(), &temp, 16);

        auto task = BluetoothLEDevice::FromBluetoothAddressAsync(mac_ulong, addr_type);
        task->Completed = ref new AsyncOperationCompletedHandler<BluetoothLEDevice^>([this, context, handler](IAsyncOperation<BluetoothLEDevice^>^ info, Windows::Foundation::AsyncStatus status) {
            switch (status) {
            case Windows::Foundation::AsyncStatus::Completed:
                if (info->GetResults() == nullptr) {
                    connect_error(context, handler, "Failed to discover device (FromBluetoothAddressAsync returned nullptr)");
                } else {
                    cookie = info->GetResults()->ConnectionStatusChanged += ref new TypedEventHandler<BluetoothLEDevice^, Object^>([this](BluetoothLEDevice^ sender, Object^ args) {
                        switch (sender->ConnectionStatus) {
                        case BluetoothConnectionStatus::Disconnected:
                            cleanup(false);

                            if (on_disconnect_handler != nullptr) {
                                on_disconnect_handler(on_disconnect_context, this, 0);
                            }
                            break;
                        }
                    });

                    this->device = info->GetResults();
                    device_discover_completed(context, handler);
                }
                break;
            case Windows::Foundation::AsyncStatus::Canceled:
                connect_error(context, handler, "Gatt connect cancelled");
                break;
            case Windows::Foundation::AsyncStatus::Error:
                stringstream buffer;
                buffer << "Failed to connect to remote device (HRESULT = " << info->ErrorCode.Value << ")";

                connect_error(context, handler, buffer.str().c_str());
                break;
            }
        });
    }
}

void WarbleGatt_Win10::disconnect() {
    cleanup();

    if (on_disconnect_handler != nullptr) {
        on_disconnect_handler(on_disconnect_context, this, 0);
    }
}

void WarbleGatt_Win10::cleanup(bool dispose) {
    for (auto it : characteristics) {
        delete it.second;
    }
    for (auto it : services) {
        delete it.second;
    }

    characteristics.clear();
    services.clear();

    if (dispose && device != nullptr) {
        device->ConnectionStatusChanged -= cookie;
        delete device;
        device = nullptr;
    }
}

void WarbleGatt_Win10::on_disconnect(void* context, FnVoid_VoidP_WarbleGattP_Int handler) {
    on_disconnect_context = context;
    on_disconnect_handler = handler;
}

bool WarbleGatt_Win10::is_connected() const {
    return device != nullptr && device->ConnectionStatus == BluetoothConnectionStatus::Connected;
}

static inline HRESULT string_to_guid(const string& uuid, GUID& raw) {
    wstring wide_uuid(uuid.begin(), uuid.end());
    wstringstream stream;
    stream << L'{' << wide_uuid << L'}';

    auto casted = ref new Platform::String(stream.str().c_str());
    return IIDFromString(casted->Data(), &raw);
}

WarbleGattChar* WarbleGatt_Win10::find_characteristic(const string& uuid) const {
    GUID raw;
    if (SUCCEEDED(string_to_guid(uuid, raw))) {
        auto it = characteristics.find(raw);
        return it == characteristics.end() ? nullptr : it->second;
    }
    return nullptr;
}

void WarbleGatt_Win10::find_characteristic_async(const char* service, const char* uuid, void* context, void(*handler)(void*, WarbleGatt*, WarbleGattChar*)) {
    GUID raw;
    if (SUCCEEDED(string_to_guid(service, raw))) {
        auto it = services.find(raw);
        if (it != services.end()) {
            auto task = it->second->GetCharacteristicsAsync();
            task->Completed = ref new AsyncOperationCompletedHandler<GattCharacteristicsResult^>([this, uuid, context, handler](IAsyncOperation<GattCharacteristicsResult^>^ info, Windows::Foundation::AsyncStatus status) {
                switch (status) {
                case Windows::Foundation::AsyncStatus::Completed: {
                    auto characteristics = info->GetResults()->Characteristics;

                    for (unsigned int j = 0; j < characteristics->Size; j++) {
                        this->characteristics.emplace(characteristics->GetAt(j)->Uuid, new WarbleGattChar_Win10(this, characteristics->GetAt(j)));
                    }

                    GUID raw;
                    if (SUCCEEDED(string_to_guid(uuid, raw))) {
                        auto it = this->characteristics.find(raw);
                        handler(context, this, it == this->characteristics.end() ? nullptr : it->second);
                    } else {
                        handler(context, this, nullptr);
                    }
                    break;
                }
                case Windows::Foundation::AsyncStatus::Canceled:
                    handler(context, this, nullptr);
                    break;
                case Windows::Foundation::AsyncStatus::Error:
                    handler(context, this, nullptr);
                    break;
                }
            });
        }
    }
}

bool WarbleGatt_Win10::service_exists(const string& uuid) const {
    GUID raw;
    return SUCCEEDED(string_to_guid(uuid, raw)) ? services.count(raw) : 0;
}

WarbleGattChar_Win10::WarbleGattChar_Win10(WarbleGatt* owner, GattCharacteristic^ characteristic) : owner(owner), characteristic(characteristic) {
    wstring wide(characteristic->Uuid.ToString()->Data());
    uuid_str = string(wide.begin(), wide.end()).substr(1, 36);
}

WarbleGattChar_Win10::~WarbleGattChar_Win10() {
    characteristic->ValueChanged -= cookie;
    characteristic = nullptr;
}

void WarbleGattChar_Win10::write_async(const uint8_t* value, uint8_t len, void* context, FnVoid_VoidP_WarbleGattCharP_CharP handler) {
    write_inner_async(GattWriteOption::WriteWithResponse, value, len, context, handler);
}

void WarbleGattChar_Win10::write_without_resp_async(const uint8_t* value, uint8_t len, void* context, FnVoid_VoidP_WarbleGattCharP_CharP handler) {
    write_inner_async(GattWriteOption::WriteWithoutResponse, value, len, context, handler);
}

void WarbleGattChar_Win10::read_async(void* context, FnVoid_VoidP_WarbleGattCharP_UbyteP_Ubyte_CharP handler) {
    auto partial = bind(handler, context, this, placeholders::_1, placeholders::_2, placeholders::_3);
    auto partial_error = bind(partial, nullptr, 0, placeholders::_1);

    auto task = characteristic->ReadValueAsync();
    task->Completed = ref new AsyncOperationCompletedHandler<GattReadResult^>([partial, partial_error](IAsyncOperation<GattReadResult^>^ result, Windows::Foundation::AsyncStatus status) {
        switch (status) {
        case Windows::Foundation::AsyncStatus::Completed: {
            Array<byte>^ wrapper = ref new Array<byte>(result->GetResults()->Value->Length);
            CryptographicBuffer::CopyToByteArray(result->GetResults()->Value, &wrapper);
            partial((uint8_t*)wrapper->Data, wrapper->Length, nullptr);
            break;
        }
        case Windows::Foundation::AsyncStatus::Canceled:
            partial_error("Gatt characteristic read cancelled");
            break;
        case Windows::Foundation::AsyncStatus::Error:
            stringstream buffer;
            buffer << "Failed to read gatt characteristic value (HRESULT = " << result->ErrorCode.Value << ")";

            partial_error(buffer.str().c_str());
            break;
        }
    });
}

void WarbleGattChar_Win10::enable_notifications_async(void* context, FnVoid_VoidP_WarbleGattCharP_CharP handler) {
    edit_notifiation_status_async(GattClientCharacteristicConfigurationDescriptorValue::Notify, context, handler);
}

void WarbleGattChar_Win10::disable_notifications_async(void* context, FnVoid_VoidP_WarbleGattCharP_CharP handler) {
    edit_notifiation_status_async(GattClientCharacteristicConfigurationDescriptorValue::None, context, handler);
}

void WarbleGattChar_Win10::on_notification_received(void* context, FnVoid_VoidP_WarbleGattCharP_UbyteP_Ubyte handler) {
    cookie = characteristic->ValueChanged += ref new TypedEventHandler<GattCharacteristic^, GattValueChangedEventArgs^>([context, handler, this](GattCharacteristic^ sender, GattValueChangedEventArgs^ obj) {
        Array<byte>^ wrapper = ref new Array<byte>(obj->CharacteristicValue->Length);
        CryptographicBuffer::CopyToByteArray(obj->CharacteristicValue, &wrapper);
        handler(context, this, (uint8_t*)wrapper->Data, wrapper->Length);
    });
}

const char* WarbleGattChar_Win10::get_uuid() const {
    return uuid_str.c_str();
}

WarbleGatt* WarbleGattChar_Win10::get_gatt() const {
    return owner;
}

#endif
