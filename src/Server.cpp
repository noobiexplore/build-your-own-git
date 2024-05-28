#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include "zstr/zstr.hpp"

int main(int argc, char *argv[])
{
    // You can use print statements as follows for debugging, they'll be visible when running tests.
    std::cout << "Logs from your program will appear here!\n";

    // Uncomment this block to pass the first stage

    if (argc < 2) {
        std::cerr << "No command provided.\n";
        return EXIT_FAILURE;
    }

    std::string command = argv[1];

    if (command == "init") {
        try {
            std::filesystem::create_directory(".git");
            std::filesystem::create_directory(".git/objects");
            std::filesystem::create_directory(".git/refs");
            std::ofstream headFile(".git/HEAD");

            if (headFile.is_open()) {
                headFile << "ref: refs/heads/main\n";
                headFile.close();
            } else {
                std::cerr << "Failed to create .git/HEAD file.\n";
                return EXIT_FAILURE;
            }
            std::cout << "Initialized git directory\n";

        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << e.what() << '\n';
            return EXIT_FAILURE;
        }
    } else if(command == "cat-file") {
        if (argc <= 3) {
            std::cerr << "Usage: path/to/your_git.sh cat-file -flag <SHA1>" << std::endl;
            return EXIT_FAILURE;
        }

        std::string flag = argv[2];
        if (flag != "-p") {
            std::cerr << "Invalid cat-file flag: expected `-p`" << std::endl;
            return EXIT_FAILURE;
        }

        // Handling the sha and creating object directory and sha object file
        std::string sha = argv[3];
        std::string obj_dir = sha.substr(0, 2);
        std::string obj_sha = sha.substr(2);

        std::string obj_path = ".git/objects/" + obj_dir + "/" + obj_sha;

        zstr::ifstream obj_file (obj_path, std::ofstream::binary);
        if (!obj_file.is_open()) {
            std::cerr << "Couldn't open the object file\n";
            return EXIT_FAILURE;
        }

        // Separate the type and byte size from the content
        char ch;
        std::string contents;
        while(obj_file.get(ch))
            if (ch == '\0') break;

        if (ch != '\0') {
            std::cerr << "No NULL character found in the blob object file\n";
            return EXIT_FAILURE;
        }
        
        // Dealt with file contents upto '\0'. Now the rest
        while (std::getline(obj_file, contents))
            std::cout << contents;

        obj_file.close();

    } else {
        std::cerr << "Unknown command " << command << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
