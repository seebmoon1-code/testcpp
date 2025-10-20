#include <iostream>
#include <string>
#include <filesystem>
#include <stdexcept> // برای مدیریت خطاها

// از std::filesystem برای سادگی در کد استفاده می‌کنیم
namespace fs = std::filesystem;

/**
 * تابعی برای ساخت تعدادی پوشه با نام‌های متوالی در یک مسیر مشخص.
 * * @param base_name نام پایه برای پوشه‌ها (مثلاً "Project")
 * @param count تعداد پوشه‌هایی که باید ساخته شوند (مثلاً 10)
 * @param base_path مسیر اصلی که پوشه‌ها در آنجا ساخته می‌شوند (مثلاً "./Output")
 * @return true در صورت موفقیت کامل، false در صورت بروز خطا.
 */
bool create_directories_start(const std::string& base_name, int count, const fs::path& base_path) {
    if (count <= 0) {
        std::cerr << "Error: Folder count must be a positive number." << std::endl;
        return false;
    }

    std::cout << "Attempting to create " << count << " folders starting with '" << base_name
              << "' in path: " << base_path.string() << std::endl;

    // 1. ابتدا مطمئن می‌شویم مسیر پایه وجود دارد
    try {
        if (!fs::exists(base_path)) {
            // از create_directories استفاده می‌کنیم تا اگر پوشه های والد هم وجود نداشت، بسازد
            if (fs::create_directories(base_path)) {
                std::cout << "Base path created: " << base_path.string() << std::endl;
            } else {
                // اگر وجود نداشت و نتوانست بسازد (به دلیل مجوز یا مشکل دیگر)
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
        // نام پوشه: ترکیب نام پایه و شماره (مثلاً Project_01, Project_02)
        std::string folder_name = base_name + "_" + (i < 10 ? "0" : "") + std::to_string(i);
        
        // مسیر کامل پوشه
        fs::path full_path = base_path / folder_name; // از عملگر / برای ترکیب مسیرها استفاده می‌کنیم

        try {
            if (fs::create_directory(full_path)) {
                std::cout << "  - Created: " << full_path.string() << std::endl;
            } else if (fs::exists(full_path)) {
                // اگر پوشه از قبل وجود داشته باشد خطا محسوب نمی‌شود
                std::cout << "  - Exists: " << full_path.string() << std::endl;
            } else {
                // اگر نتوانست بسازد و وجود هم نداشت (خطای واقعی)
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

int main() {
    // --- مثال تست ---

    // 1. آرگومان اول: نام پایه پوشه
    std::string folder_base = "Data_Set"; 
    
    // 2. آرگومان دوم: تعداد پوشه
    int folder_count = 5; 
    
    // 3. آرگومان سوم: جای ساخت پوشه (مسیر نسبی یا مطلق)
    // برای مثال: یک پوشه به نام 'output_dirs' در کنار فایل اجرایی می‌سازد.
    fs::path destination_path = "C:\\Temp\\My_Test_Folders"; // می توانید از "./Output" هم استفاده کنید
    
    
    // فراخوانی تابع با آرگومان‌های داده شده
    if (create_directories_start(folder_base, folder_count, destination_path)) {
        std::cout << "\nOperation completed successfully." << std::endl;
    } else {
        std::cerr << "\nOperation completed with some errors." << std::endl;
    }

    return 0;
}
