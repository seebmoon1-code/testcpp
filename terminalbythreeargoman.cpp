#include <iostream>
#include <string>
#include <filesystem>
#include <stdexcept>
#include <cstdlib> // برای تابع exit

namespace fs = std::filesystem;

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

int main(int argc, char* argv[]) {
    // تعداد آرگومان‌های مورد نیاز: 
    // 1 (اسم خود برنامه) + 3 (نام، تعداد، مسیر) = 4
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <base_name> <count> <base_path>" << std::endl;
        std::cerr << "Example: " << argv[0] << " Data 10 /home/user/output" << std::endl;
        return 1; // خروج با کد خطا
    }

    // 1. آرگومان اول: نام پایه پوشه (argv[1])
    std::string folder_base = argv[1];

    // 2. آرگومان دوم: تعداد پوشه (argv[2])
    int folder_count;
    try {
        // تبدیل رشته به عدد صحیح
        folder_count = std::stoi(argv[2]); 
    } catch (const std::exception& e) {
        std::cerr << "Error: Invalid number for count. Please enter an integer." << std::endl;
        return 1;
    }
    
    // 3. آرگومان سوم: جای ساخت پوشه (argv[3])
    // توجه: بسته به سیستم‌عامل، باید مسیر رو به درستی وارد کنید (مثلاً "C:/Users/User/Desktop/god" در ویندوز یا "/home/user/desktop/god" در لینوکس)
    fs::path destination_path = argv[3];
    
    
    // فراخوانی تابع با آرگومان‌های خط فرمان
    if (create_directories_start(folder_base, folder_count, destination_path)) {
        std::cout << "\nOperation completed successfully." << std::endl;
    } else {
        std::cerr << "\nOperation completed with some errors." << std::endl;
        return 1;
    }

    return 0; // خروج موفق
}
