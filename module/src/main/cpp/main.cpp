#include <cstring>
#include <thread>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cinttypes>
#include "hack.h"
#include "zygisk.hpp"
#include "game.h"
#include "log.h"

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;


// Define the variable with your default
const char *GamePackageName = "com.your.default.package";

void tryReadPackageNameFromFile() {
        // Try storage root first (most user-friendly)
        const char* possiblePaths[] = {
            "/sdcard/target_package.txt",
            "/storage/emulated/0/target_package.txt",
            "/data/adb/modules/zygisk_il2cppdumper/target_package.txt"
        };
        
        for (const char* path : possiblePaths) {
            if (access(path, R_OK) == 0) {
                std::ifstream file(path);
                if (file.is_open()) {
                    std::string line;
                    if (std::getline(file, line)) {
                        // Remove comments (lines starting with #)
                        size_t commentPos = line.find('#');
                        if (commentPos != std::string::npos) {
                            line = line.substr(0, commentPos);
                        }
                        
                        // Trim whitespace
                        line.erase(0, line.find_first_not_of(" \t\r\n"));
                        line.erase(line.find_last_not_of(" \t\r\n") + 1);
                        
                        if (!line.empty() && line.find_first_not_of('.') != std::string::npos) {
                            static std::string loadedName = line;
                            GamePackageName = loadedName.c_str();
                            LOGI("Il2CppDumper: Loaded package name from %s: %s", 
                                 path, GamePackageName);
                            file.close();
                            return;
                        }
                    }
                    file.close();
                }
            }
        }
        
        LOGI("Il2CppDumper: Using default package name: %s", GamePackageName);
    }
}

class MyModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;

        // Read package name from file
        tryReadPackageNameFromFile();
        
        LOGI("Il2CppDumper: Module loaded for package: %s", GamePackageName);
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        auto package_name = env->GetStringUTFChars(args->nice_name, nullptr);
        auto app_data_dir = env->GetStringUTFChars(args->app_data_dir, nullptr);
        preSpecialize(package_name, app_data_dir);
        env->ReleaseStringUTFChars(args->nice_name, package_name);
        env->ReleaseStringUTFChars(args->app_data_dir, app_data_dir);
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (enable_hack) {
            std::thread hack_thread(hack_prepare, game_data_dir, data, length);
            hack_thread.detach();
        }
    }

private:
    Api *api;
    JNIEnv *env;
    bool enable_hack;
    char *game_data_dir;
    void *data;
    size_t length;

    void preSpecialize(const char *package_name, const char *app_data_dir) {
        if (strcmp(package_name, GamePackageName) == 0) {
            LOGI("detect game: %s", package_name);
            enable_hack = true;
            game_data_dir = new char[strlen(app_data_dir) + 1];
            strcpy(game_data_dir, app_data_dir);

#if defined(__i386__)
            auto path = "zygisk/armeabi-v7a.so";
#endif
#if defined(__x86_64__)
            auto path = "zygisk/arm64-v8a.so";
#endif
#if defined(__i386__) || defined(__x86_64__)
            int dirfd = api->getModuleDir();
            int fd = openat(dirfd, path, O_RDONLY);
            if (fd != -1) {
                struct stat sb{};
                fstat(fd, &sb);
                length = sb.st_size;
                data = mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, 0);
                close(fd);
            } else {
                LOGW("Unable to open arm file");
            }
#endif
        } else {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        }
    }
};

REGISTER_ZYGISK_MODULE(MyModule)
