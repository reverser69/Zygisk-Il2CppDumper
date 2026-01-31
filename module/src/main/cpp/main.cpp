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
        const char* possiblePaths[] = {
            "/sdcard/target_package.txt", // not readable, for demonstration and future use
            "/data/adb/modules/zygisk_il2cppdumper/target_package.txt"
        };
        
        for (const char* path : possiblePaths) {
            LOGI("Searching for target_package.txt in: %s", path);
            FILE* file = fopen(path, "r");
            if (file) {
                char buffer[256];
                if (fgets(buffer, sizeof(buffer), file)) {
                    // Clean up the line
                    char* line = buffer;
                    
                    // Remove trailing newline
                    size_t len = strlen(line);
                    if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
                    if (len > 0 && line[len-1] == '\r') line[len-1] = '\0';
                    
                    // Remove comments
                    char* comment = strchr(line, '#');
                    if (comment) *comment = '\0';
                    
                    // Trim whitespace
                    while (*line == ' ' || *line == '\t') line++;
                    char* end = line + strlen(line) - 1;
                    while (end > line && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
                        *end = '\0';
                        end--;
                    }
                    
                    if (strlen(line) > 0) {
                        static char loadedName[256];
                        strncpy(loadedName, line, sizeof(loadedName)-1);
                        loadedName[sizeof(loadedName)-1] = '\0';
                        GamePackageName = loadedName;
                        LOGI("Loaded package name from %s: %s", path, GamePackageName);
                        fclose(file);
                        return;
                    }
                }
                fclose(file);
            }
        }
        LOGI("No target_package.txt found, using default: %s", GamePackageName);
    }


class MyModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
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
        
        // Read package name from file
        static bool fileRead = false;
        if (!fileRead) {
            tryReadPackageNameFromFile();
            fileRead = true;
        }
        
        LOGD("[Il2CppDumper] Checking app: %s against target: %s", app_data_dir, package_name);
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
