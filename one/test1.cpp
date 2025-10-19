
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <limits>
#include <algorithm>

// تعریف ساختار داده
using Record = std::vector<std::string>;
using DatabaseTable = std::vector<Record>;

class dbc_c_Database {
private:
    std::string dbName;
    std::string cfgFileName;
    std::string dataFileName;
    std::vector<std::string> fieldNames;
    DatabaseTable data;
    size_t numFields = 0; // تعداد ستون‌ها به صورت پویا

    // --- توابع کمکی ---

    std::string get_user_input(const std::string& prompt) {
        // (همان تابع قبلی برای ورودی تمیز کاربر)
        std::string input;
        std::cout << prompt;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::getline(std::cin, input);
        
        input.erase(0, input.find_first_not_of(" \t\n\r\f\v"));
        input.erase(input.find_last_not_of(" \t\n\r\f\v") + 1);
        return input;
    }

    Record parseLine(const std::string& line) const {
        Record record;
        std::stringstream ss(line);
        std::string segment;
        while (std::getline(ss, segment, ',')) {
            record.push_back(segment);
        }
        return record;
    }
    
    // --- مدیریت فایل تنظیمات (Configuration) ---

    void loadConfig() {
        std::ifstream infile(cfgFileName);
        if (!infile.is_open()) return;

        std::string line;
        // سطر اول: تعداد فیلدها
        if (std::getline(infile, line)) {
            try {
                numFields = std::stoul(line);
            } catch (...) {
                numFields = 0; // خطا در خواندن
            }
        }
        // سطر دوم: نام فیلدها (CSV-Format)
        if (std::getline(infile, line)) {
            fieldNames = parseLine(line);
            // اطمینان از همخوانی تعداد فیلدها با نام‌ها
            if (fieldNames.size() != numFields) {
                std::cerr << "Warning: Field name count mismatch in config." << std::endl;
                fieldNames.resize(numFields);
                for(size_t i=0; i<numFields; ++i) {
                     fieldNames[i] = "Col" + std::to_string(i + 1); // پیش‌فرض
                }
            }
        }
        infile.close();
    }

    void saveConfig() const {
        std::ofstream outfile(cfgFileName);
        if (!outfile.is_open()) {
            std::cerr << "Error: Could not open config file for saving." << std::endl;
            return;
        }

        // 1. ذخیره تعداد فیلدها
        outfile << numFields << "\n";
        
        // 2. ذخیره نام فیلدها
        for (size_t i = 0; i < fieldNames.size(); ++i) {
            outfile << fieldNames[i] << (i < fieldNames.size() - 1 ? "," : "");
        }
        outfile << "\n";
        outfile.close();
    }

    // --- مدیریت فایل داده (Data) ---

    void loadData() {
        if (numFields == 0) return; // نمی‌تواند بدون پیکربندی بارگذاری کند

        std::ifstream infile(dataFileName);
        if (!infile.is_open()) return;

        data.clear();
        std::string line;
        while (std::getline(infile, line)) {
            Record newRecord = parseLine(line);
            // اطمینان از حفظ اندازه صحیح رکوردها
            newRecord.resize(numFields, ""); 
            data.push_back(newRecord);
        }
        infile.close();
    }

    void saveData() const {
        if (numFields == 0) return;

        std::ofstream outfile(dataFileName);
        if (!outfile.is_open()) {
            std::cerr << "Error: Could not open data file for saving." << std::endl;
            return;
        }

        // ذخیره داده‌های واقعی (بدون هدر، چون هدر در فایل تنظیمات است)
        for (const auto& record : data) {
            for (size_t i = 0; i < numFields; ++i) {
                outfile << record[i] << (i < numFields - 1 ? "," : "");
            }
            outfile << "\n";
        }
        outfile.close();
        std::cout << "Data saved successfully to " << dataFileName << std::endl;
    }

    // --- تعامل برای تنظیم نام فیلدها ---

    void setFieldNamesInteractively(size_t N) {
        fieldNames.assign(N, "");
        numFields = N;
        std::cout << "\n--- تنظیم نام " << N << " ستون دیتابیس (" << dbName << ") ---\n";
        bool continueSetup = true;

        for (size_t i = 0; i < N; ++i) {
            std::string fieldDefaultName = "Col" + std::to_string(i + 1);
            
            if (!continueSetup) {
                fieldNames[i] = fieldDefaultName;
                continue; 
            }
            
            std::string prompt = "نام ستون " + std::to_string(i + 1) + " (" + fieldDefaultName + ") چی باشه؟ (اینتر برای رد شدن) ";
            std::string input = get_user_input(prompt);
            
            if (input.empty()) {
                fieldNames[i] = fieldDefaultName; 
                
                std::cout << "ستون خالی ماند. آیا مایل به تعریف ستون‌های بعدی هستید؟ (Y/n، پیش‌فرض: Y) ";
                std::string cont;
                std::getline(std::cin, cont);
                
                if (cont.empty() || cont[0] == 'n' || cont[0] == 'N') {
                    continueSetup = false;
                }
            } else {
                fieldNames[i] = input;
            }
        }
        saveConfig(); 
        std::cout << "--- تنظیمات ستون‌ها با موفقیت ذخیره شد. ---\n";
    }

public:
    // سازنده: dbc_c_moon
    dbc_c_Database(const std::string& name) : dbName(name) {
        cfgFileName = "dbc_c_" + name + ".cfg";
        dataFileName = "dbc_c_" + name + ".dat";
        
        loadConfig(); // ابتدا سعی می‌کند پیکربندی را بارگذاری کند
        if (numFields > 0) {
            loadData(); // اگر پیکربندی بود، داده‌ها را هم بارگذاری می‌کند
            std::cout << "دیتابیس '" << name << "' با " << numFields << " ستون بارگذاری شد.\n";
        } else {
            std::cout << "دیتابیس '" << name << "' ایجاد شد. لطفا تعداد ستون‌ها را تعریف کنید (مانند: moon_c_4).\n";
        }
    }
    
    // متد کلیدی جدید: moon_c_4
    void configureColumns(size_t N) {
        if (N == 0) {
             std::cerr << "تعداد ستون‌ها باید حداقل 1 باشد." << std::endl;
             return;
        }
        
        if (numFields > 0) {
             std::cout << "هشدار: دیتابیس قبلا پیکربندی شده است. تغییر پیکربندی، داده‌های موجود را پاک می‌کند." << std::endl;
             // در یک پروژه واقعی، اینجا باید تاییدیه گرفته شود.
             data.clear();
        }
        
        setFieldNamesInteractively(N);
        std::cout << "پیکربندی دیتابیس با " << N << " ستون نهایی شد.\n";
    }

    // متد ورود داده جدید
    void enterNewRecord() {
        if (numFields == 0) {
             std::cerr << "خطا: ابتدا باید تعداد ستون‌ها را تعریف کنید (مانند: moon_c_4)." << std::endl;
             return;
        }

        Record newRecord(numFields, ""); 
        std::cout << "\n--- وارد کردن داده‌های جدید (رکورد " << data.size() + 1 << ") ---\n";
        
        for (size_t i = 0; i < numFields; ++i) {
            std::string prompt = "ورودی برای [" + fieldNames[i] + "]: ";
            std::string input = get_user_input(prompt);

            if (!input.empty()) {
                newRecord[i] = input;
            }
            
            // اگر آخرین فیلد را پر کرد و اینتر زد، رکورد تکمیل شده و ذخیره می‌شود
            if (i == numFields - 1) { 
                data.push_back(newRecord); 
                saveData(); 
                std::cout << "\nرکورد " << data.size() << " تکمیل و ذخیره شد. \n";
            }
        }
    }

    // متد نمایش
    void displayDatabase() const {
        if (numFields == 0) {
             std::cerr << "خطا: دیتابیس هنوز پیکربندی نشده است." << std::endl;
             return;
        }
        std::cout << "\n--- نمایش دیتابیس (" << dbName << ") با " << numFields << " ستون ---\n";
        
        // نمایش هدر
        std::cout << "| #\t";
        for (const auto& name : fieldNames) {
            std::cout << "| " << name << "\t";
        }
        std::cout << "|\n";
        
        // نمایش جداکننده
        for (size_t i = 0; i <= numFields; ++i) {
            std::cout << "----------";
        }
        std::cout << "-\n";

        // نمایش داده‌ها
        int rowNum = 1;
        for (const auto& record : data) {
            std::cout << "| " << rowNum++ << "\t";
            for (const auto& value : record) {
                std::cout << "| " << value << "\t";
            }
            std::cout << "|\n";
        }
        std::cout << "--------------------------------\n";
    }
};

// --- حلقه اصلی برنامه ---

int main() {
    // 1. ایجاد دیتابیس با نام 'moon'
    dbc_c_Database myDB("moon"); 
    
    std::string choice;
    bool running = true;

    // **حلقه اصلی برنامه (Main Loop)**
    while (running) {
        std::cout << "\n============================================\n";
        std::cout << "دیتابیس MOON - عملیات:\n";
        std::cout << "1. پیکربندی ستون‌ها (مثال: moon_c_4)\n"; 
        std::cout << "2. وارد کردن رکورد جدید\n";
        std::cout << "3. نمایش کل دیتابیس\n";
        std::cout << "4. خروج\n";
        std::cout << "گزینه خود را وارد کنید (1-4) یا دستور [DB_NAME_c_N]: ";
        
        std::cin >> choice; 
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // پاکسازی بافر
        
        if (choice == "1") {
            // شبیه سازی دستور moon_c_N
            std::string countStr = myDB.dbName + "_c_";
            std::cout << "لطفا تعداد ستون‌ها را وارد کنید (فقط عدد N): ";
            std::string N_input;
            std::getline(std::cin, N_input);
            try {
                size_t N = std::stoul(N_input);
                myDB.configureColumns(N);
            } catch (...) {
                std::cerr << "خطا: ورودی نامعتبر برای تعداد ستون.\n";
            }
            
        } else if (choice == "2") {
            myDB.enterNewRecord();
        } else if (choice == "3") {
            myDB.displayDatabase();
        } else if (choice == "4") {
            running = false;
        } else {
            // اگر کاربر مستقیماً دستور moon_c_N را وارد کند
            if (choice.size() > myDB.dbName.size() + 3 && choice.substr(0, myDB.dbName.size() + 3) == myDB.dbName + "_c_") {
                 try {
                    size_t N = std::stoul(choice.substr(myDB.dbName.size() + 3));
                    myDB.configureColumns(N);
                 } catch (...) {
                    std::cerr << "خطا: فرمت دستور نامعتبر است. از moon_c_N استفاده کنید (N عدد باشد).\n";
                 }
            } else {
                std::cout << "گزینه یا دستور نامعتبر. دوباره تلاش کنید.\n";
            }
        }
    }

    std::cout << "خداحافظ. دیتابیس بسته شد.\n";
    return 0;
}
