
#include <iostream>
#include <string>
#include <filesystem>
#include <stdexcept>
#include <cstdlib>

namespace fs = std::filesystem;

// [کدهای create_directories_start و main شما در اینجا قرار می‌گیرند...]

// تابع اصلی ساخت پوشه‌ها (بدون تغییر)
bool create_directories_start(const std::string& base_name, int count, const fs::path& base_path) {
    // ... همان منطق قبلی شما ...
    if (count <= 0) {
        std::cerr << "Error: Folder count must be a positive number." << std::endl;
        return false;
    }

    std::cout << "Attempting to create " << count << " folders starting with '" << base_name
              << "' in path: " << base_path.string() << std::endl;

    // 1. ابتدا مطمئن می‌شویم مسیر پایه وجود دارد
    try {
        if (!fs::exists(base_path)) {
            if (fs::create_directories(base_path)) {
                std::cout << "Base path created: " << base_path.string() << std::endl;
            } else {
                std::cerr << "Error: Failed to create base path: " << base_path.string() << std::endl;
                return false;
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem Error checking/creating base path: " << e.what() << std::endl;
        return false;
    }

    // 2. ساخت پوشه‌های متوالی
    bool all_successful = true;
    for (int i = 1; i <= count; ++i) {
        std::string folder_name = base_name + "_" + (i < 10 ? "0" : "") + std::to_string(i);
        fs::path full_path = base_path / folder_name; 

        try {
            if (fs::create_directory(full_path)) {
                std::cout << "  - Created: " << full_path.string() << std::endl;
            } else if (fs::exists(full_path)) {
                std::cout << "  - Exists: " << full_path.string() << std::endl;
            } else {
                std::cerr << "  - Failed to create: " << full_path.string() << std::endl;
                all_successful = false;
            }
        } catch (const fs::filesystem_error& e) {
            std::cerr << "  - Filesystem Error creating " << full_path.string() << ": " << e.what() << std::endl;
            all_successful = false;
        }
    }

    return all_successful;
}

// ----------------------------------------------------------------------------------------------------

/**
 * پوشه‌های خالی را به صورت بازگشتی از مسیر پایه حذف می‌کند.
 * * @param base_path مسیر اصلی برای شروع جستجو.
 * @return تعداد پوشه‌های خالی حذف شده.
 */
int remove_empty_directories(const fs::path& base_path) {
    if (!fs::exists(base_path) || !fs::is_directory(base_path)) {
        std::cerr << "Error: Path does not exist or is not a directory: " << base_path.string() << std::endl;
        return 0;
    }

    std::cout << "\nScanning for empty directories in: " << base_path.string() << std::endl;

    int deleted_count = 0;

    // fs::recursive_directory_iterator برای پیمایش در تمامی زیرپوشه‌ها
    try {
        // از back() استفاده می‌کنیم چون می‌خواهیم از عمیق‌ترین پوشه‌ها شروع کنیم
        // و بالا بیاییم تا مطمئن شویم پوشه‌های خالی که حذف شده‌اند، باعث خالی شدن پوشه‌های والدشان می‌شوند.
        std::vector<fs::path> dirs_to_check;
        for (const auto& entry : fs::recursive_directory_iterator(base_path)) {
            if (entry.is_directory()) {
                dirs_to_check.push_back(entry.path());
            }
        }

        // پیمایش از انتها به ابتدا (از عمیق‌ترین زیرپوشه‌ها)
        std::reverse(dirs_to_check.begin(), dirs_to_check.end());

        // اضافه کردن مسیر پایه (base_path) برای بررسی نهایی
        // زیرا recursive_directory_iterator خود مسیر پایه را شامل نمی‌شود.
        dirs_to_check.push_back(base_path); 

        for (const auto& dir_path : dirs_to_check) {
            // یک بار دیگر بررسی می‌کنیم که آیا پوشه هنوز وجود دارد
            if (fs::exists(dir_path) && fs::is_directory(dir_path)) {
                // چک می‌کنیم که آیا پوشه خالی است یا نه (با استفاده از iterator)
                if (fs::is_empty(dir_path)) {
                    // تلاش برای حذف پوشه
                    if (fs::remove(dir_path)) {
                        std::cout << "  - Deleted empty folder: " << dir_path.string() << std::endl;
                        deleted_count++;
                    } else {
                        // در صورت بروز مشکل در حذف (مثلاً مجوزها)
                        std::cerr << "  - Warning: Failed to delete " << dir_path.string() << std::endl;
                    }
                }
            }
        }

    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem Error during directory cleanup: " << e.what() << std::endl;
    }
    
    std::cout << "\nCleanup complete. Total empty folders deleted: " << deleted_count << std::endl;
    return deleted_count;
}

// ----------------------------------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    // تعداد آرگومان‌های مورد نیاز برای حالت ساخت پوشه: 4
    // تعداد آرگومان‌های مورد نیاز برای حالت حذف پوشه خالی: 3
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage for Creation: " << argv[0] << " <base_name> <count> <base_path>" << std::endl;
        std::cerr << "Usage for Cleanup: " << argv[0] << " cleanup <base_path>" << std::endl;
        std::cerr << "Example Creation: " << argv[0] << " Data 10 /home/user/output" << std::endl;
        std::cerr << "Example Cleanup: " << argv[0] << " cleanup /home/user/output/Data_01" << std::endl;
        return 1; // خروج با کد خطا
    }
    
    // حالت ۱: ساخت پوشه (آرگومان‌ها: نام، تعداد، مسیر)
    if (argc == 4) {
        std::string folder_base = argv[1];
        int folder_count;
        try {
            folder_count = std::stoi(argv[2]); 
        } catch (const std::exception& e) {
            std::cerr << "Error: Invalid number for count. Please enter an integer." << std::endl;
            return 1;
        }
        fs::path destination_path = argv[3];
        
        if (create_directories_start(folder_base, folder_count, destination_path)) {
            std::cout << "\nCreation operation completed successfully." << std::endl;
        } else {
            std::cerr << "\nCreation operation completed with some errors." << std::endl;
            return 1;
        }
        
    // حالت ۲: پاکسازی (آرگومان‌ها: "cleanup"، مسیر)
    } else if (argc == 3 && std::string(argv[1]) == "cleanup") {
        fs::path destination_path = argv[2];
        
        if (remove_empty_directories(destination_path) > 0) {
             std::cout << "\nCleanup operation completed successfully." << std::endl;
        } else if (fs::exists(destination_path)) {
            std::cout << "\nCleanup operation completed. No empty folders were deleted." << std::endl;
        } else {
             std::cerr << "\nCleanup operation failed." << std::endl;
             return 1;
        }
        
    } else {
        // این حالت نباید رخ دهد اگر شرط argc != 4 در ابتدا درست کار کند، 
        // اما برای اطمینان از خروجی خطا آن را نگه می‌داریم.
        std::cerr << "Invalid combination of arguments." << std::endl;
        return 1;
    }
    
    return 0; // خروج موفق
}
