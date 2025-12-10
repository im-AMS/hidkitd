#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// This struct will hold our parsed command-line arguments.
typedef struct {
    long vendorID;
    long productID;
    long usagePage;
    long usage;
    const char *productName;
    const char *deviceAddress;
    const char *onConnectScript;
    const char *onDisconnectScript;
} AppConfig;

// A simple function to run a user-provided script.
void run_script(const char *scriptPath) {
    if (!scriptPath) return; // Do nothing if the script path is not provided
    char command[2048];
    snprintf(command, sizeof(command), "\"%s\"", scriptPath);
    printf("DAEMON: Executing command: %s\n", command);
    fflush(stdout);
    system(command);
}

// Callback for device connection.
void deviceConnected(void *refcon, io_iterator_t iterator) {
    AppConfig *config = (AppConfig *)refcon;
    io_service_t service;
    while ((service = IOIteratorNext(iterator))) {
        printf("DAEMON: Received connect (matched) event.\n");
        fflush(stdout);
        run_script(config->onConnectScript);
        IOObjectRelease(service);
    }
}

// Callback for device disconnection.
void deviceDisconnected(void *refcon, io_iterator_t iterator) {
    AppConfig *config = (AppConfig *)refcon;
    io_service_t service;
    while ((service = IOIteratorNext(iterator))) {
        printf("DAEMON: Received disconnect (terminated) event.\n");
        fflush(stdout);
        run_script(config->onDisconnectScript);
        IOObjectRelease(service);
    }
}

// Helper function to build the IOKit matching dictionary from user flags.
CFMutableDictionaryRef createMatchingDictionary(const AppConfig *config) {
    CFMutableDictionaryRef dict = IOServiceMatching("IOHIDUserDevice");
    if (!dict) {
        fprintf(stderr, "DAEMON_ERROR: IOServiceMatching failed.\n");
        return NULL;
    }

    if (config->vendorID > 0) {
        CFNumberRef num = CFNumberCreate(kCFAllocatorDefault, kCFNumberLongType, &config->vendorID);
        CFDictionarySetValue(dict, CFSTR("VendorID"), num);
        CFRelease(num);
    }
    if (config->productID > 0) {
        CFNumberRef num = CFNumberCreate(kCFAllocatorDefault, kCFNumberLongType, &config->productID);
        CFDictionarySetValue(dict, CFSTR("ProductID"), num);
        CFRelease(num);
    }
    if (config->usagePage > 0) {
        CFNumberRef num = CFNumberCreate(kCFAllocatorDefault, kCFNumberLongType, &config->usagePage);
        CFDictionarySetValue(dict, CFSTR("PrimaryUsagePage"), num);
        CFRelease(num);
    }
    if (config->usage > 0) {
        CFNumberRef num = CFNumberCreate(kCFAllocatorDefault, kCFNumberLongType, &config->usage);
        CFDictionarySetValue(dict, CFSTR("PrimaryUsage"), num);
        CFRelease(num);
    }
    if (config->productName) {
        CFStringRef str = CFStringCreateWithCString(kCFAllocatorDefault, config->productName, kCFStringEncodingUTF8);
        CFDictionarySetValue(dict, CFSTR("Product"), str);
        CFRelease(str);
    }
    if (config->deviceAddress) {
        CFStringRef str = CFStringCreateWithCString(kCFAllocatorDefault, config->deviceAddress, kCFStringEncodingUTF8);
        CFDictionarySetValue(dict, CFSTR("DeviceAddress"), str);
        CFRelease(str);
    }
    return dict;
}

void print_help(const char *prog_name) {
    printf("hidkitd: A persistent daemon to run scripts on device events.\n");
    printf("NOTE: This tool is specifically designed to monitor `IOHIDUserDevice` objects,\n");
    printf("      such as keyboards, mice, game controllers, and other custom HID hardware.\n\n");
    printf("Usage: %s [FILTERS] [ACTIONS]\n\n", prog_name);
    printf("FILTERS (at least one is required, multiple are combined with AND logic):\n");
    printf("  --vendor-id <id>       Match by USB Vendor ID (number).\n");
    printf("  --product-id <id>      Match by USB Product ID (number).\n");
    printf("  --name <string>        Match by Product Name (string).\n");
    printf("  --address <mac_string> Match by Bluetooth Device Address (string).\n");
    printf("  --usage-page <id>      Match by HID Primary Usage Page (number).\n");
    printf("  --usage <id>           Match by HID Primary Usage (number).\n\n");
    printf("ACTIONS (at least one is required):\n");
    printf("  --on-connect <path>    Script to run when the device connects.\n");
    printf("  --on-disconnect <path> Script to run when the device disconnects.\n\n");
    printf("HELP:\n");
    printf("  --help                 Display this help message and exit.\n\n");
    printf("HOW TO FIND FILTER VALUES:\n");
    printf("  1. Connect your device.\n");
    printf("  2. Open the Terminal application.\n");
    printf("  3. Run the command: `ioreg -r -c IOHIDDevice`\n");
    printf("  4. Search the output for your device's name (e.g., 'Logitech Mouse'). You will\n");
    printf("     see a block of properties for it. Look for the following keys to use as filters:\n");
    printf("     - \"ProductID\" = 1234\n");
    printf("     - \"VendorID\" = 5678\n");
    printf("     - \"Product\" = \"My Cool Keyboard\"\n");
    printf("     - \"DeviceAddress\" = \"ab-cd-ef-12-34-56\"\n");
    printf("     - \"PrimaryUsagePage\" = 1\n");
    printf("     - \"PrimaryUsage\" = 6\n\n");
    printf("EXAMPLE (for a standard keyboard to avoid multiple triggers from media keys):\n");
    printf("  %s \\\n", prog_name);
    printf("    --name \"My Custom Keyboard\" \\\n");
    printf("    --usage-page 1 --usage 6 \\\n");
    printf("    --on-connect /path/to/connect_script.sh \\\n");
    printf("    --on-disconnect /path/to/disconnect_script.sh\n\n");
}

int main(int argc, const char * argv[]) {
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--help") == 0)) {
        print_help(argv[0]);
        return 0;
    }

    AppConfig config = {0};
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) { fprintf(stderr, "Error: Flag %s is missing a value. Use --help.\n", argv[i]); return 1; }
        const char *flag = argv[i];
        const char *val = argv[i+1];
        if (strcmp(flag, "--vendor-id") == 0) config.vendorID = strtol(val, NULL, 10);
        else if (strcmp(flag, "--product-id") == 0) config.productID = strtol(val, NULL, 10);
        else if (strcmp(flag, "--usage-page") == 0) config.usagePage = strtol(val, NULL, 10);
        else if (strcmp(flag, "--usage") == 0) config.usage = strtol(val, NULL, 10);
        else if (strcmp(flag, "--name") == 0) config.productName = val;
        else if (strcmp(flag, "--address") == 0) config.deviceAddress = val;
        else if (strcmp(flag, "--on-connect") == 0) config.onConnectScript = val;
        else if (strcmp(flag, "--on-disconnect") == 0) config.onDisconnectScript = val;
        else { fprintf(stderr, "Error: Unknown flag %s. Use --help.\n", flag); return 1; }
    }

    if (config.vendorID == 0 && config.productID == 0 && !config.productName && !config.deviceAddress && config.usagePage == 0 && config.usage == 0) {
        fprintf(stderr, "Error: You must provide at least one filter. Use --help.\n"); return 1;
    }
    if (!config.onConnectScript && !config.onDisconnectScript) {
        fprintf(stderr, "Error: You must provide at least one action script. Use --help.\n"); return 1;
    }

    printf("DAEMON: Starting up...\n");
    fflush(stdout);

    IONotificationPortRef notifyPort = IONotificationPortCreate(kIOMainPortDefault);
    CFRunLoopSourceRef runLoopSource = IONotificationPortGetRunLoopSource(notifyPort);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource, kCFRunLoopDefaultMode);

    CFMutableDictionaryRef matchDict = createMatchingDictionary(&config);
    io_iterator_t matchedIterator;
    IOServiceAddMatchingNotification(notifyPort, kIOMatchedNotification, matchDict, deviceConnected, &config, &matchedIterator);
    deviceConnected(&config, matchedIterator);

    CFMutableDictionaryRef termDict = createMatchingDictionary(&config);
    io_iterator_t terminatedIterator;
    IOServiceAddMatchingNotification(notifyPort, kIOTerminatedNotification, termDict, deviceDisconnected, &config, &terminatedIterator);
    deviceDisconnected(&config, terminatedIterator);

    printf("DAEMON: Monitoring started.\n");
    fflush(stdout);
    CFRunLoopRun();

    return 0; // Never reached
}
