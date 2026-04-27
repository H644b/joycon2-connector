#pragma once
// ViGEm Manager - Singleton wrapping ViGEm client lifecycle (Windows only)

#ifdef _WIN32

#include <ViGEm/Client.h>
#include <ViGEm/Common.h>
#include <iostream>
#include <atomic>

class ViGEmManager {
public:
    static ViGEmManager& Instance() {
        static ViGEmManager inst;
        return inst;
    }

    bool Initialize() {
        if (client != nullptr) return true;

        client = vigem_alloc();
        if (!client) {
            connected = false;
            return false;
        }

        auto ret = vigem_connect(client);
        if (!VIGEM_SUCCESS(ret)) {
            vigem_free(client);
            client = nullptr;
            connected = false;
            return false;
        }

        connected = true;
        return true;
    }

    void Shutdown() {
        if (client) {
            vigem_disconnect(client);
            vigem_free(client);
            client = nullptr;
        }
        connected = false;
    }

    bool IsConnected() const { return connected; }
    PVIGEM_CLIENT GetClient() const { return client; }

    ~ViGEmManager() {
        Shutdown();
    }

private:
    ViGEmManager() = default;
    PVIGEM_CLIENT client = nullptr;
    std::atomic<bool> connected{ false };
};

// Helper used by VirtualController.h's ViGEmController
inline PVIGEM_CLIENT _GetViGEmClient() {
    return ViGEmManager::Instance().GetClient();
}

#else // non-Windows stub

class ViGEmManager {
public:
    static ViGEmManager& Instance() { static ViGEmManager inst; return inst; }
    bool Initialize() { return false; }
    void Shutdown()   {}
    bool IsConnected() const { return false; }
};

#endif // _WIN32

