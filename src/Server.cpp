#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>

#include "zstr/zstr.hpp"
#include "sha1.hpp"

int main(int argc, char *argv[])
{
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
            std::cout << "Couldn't create directory\ne";
            std::cerr << e.what() << '\n';
            return EXIT_FAILURE;
        }
    } else if(command == "cat-file") {
        if (argc <= 3) {
            std::cerr << "Usage: path/to/your_git.sh cat-file -flag <SHA1>" << std::endl;
            return EXIT_FAILURE;
        }

        std::string cat_flag = argv[2];
        if (cat_flag != "-p") {
            std::cerr << "Invalid cat-file flag: expected `-p`" << std::endl;
            return EXIT_FAILURE;
        }

        // Handling the sha and creating object directory and sha object file
        std::string obj_full_sha = argv[3];
        std::string obj_dir = obj_full_sha.substr(0, 2);
        std::string obj_sha = obj_full_sha.substr(2);

        std::string obj_path = ".git/objects/" + obj_dir + "/" + obj_sha;

        zstr::ifstream obj_file (obj_path, std::ios::binary);
        if (!obj_file.is_open()) {
            std::cerr << "Couldn't open the object file\n";
            return EXIT_FAILURE;
        }

        // Separate the type and byte size from the content
        char ch;
        while(obj_file.get(ch))
            if (ch == '\0') break;

        if (ch != '\0') {
            std::cerr << "No NULL character found in the blob object file\n";
            return EXIT_FAILURE;
        }
        
        // Dealt with file contents upto '\0'. Now the rest
        std::string obj_contents{std::istreambuf_iterator<char>(obj_file),
                            std::istreambuf_iterator<char>()};
        std::cout << obj_contents;
        obj_file.close();

    } else if (command == "hash-object") {
        if (argc <= 3) {
            std::cerr << "Usage: path/to/your_git hash-object -w <text-file>\n";
            return EXIT_FAILURE;
        }
        std::string hash_flag = argv[2];
        
        if (hash_flag != "-w") {
            std::cerr << "Invalid hash-object flag: expected `-w`\n";
            return EXIT_FAILURE;
        }

        // Read the file content to a string
        std::string file_path = argv[3];
        std::ifstream content_file (file_path, std::ios::binary);
        if (!content_file) {
            std::cerr << "Failed to open the content file\n";
            return EXIT_FAILURE;
        }
        std::string file_contents((std::istreambuf_iterator<char>(content_file)), 
                            std::istreambuf_iterator<char>());

        // SHA1 calculation and printing
        SHA1 content_file_sha;
        content_file_sha.update(file_contents);
        std::string file_sha = content_file_sha.final();
        std::cout << file_sha << std::endl;

        // Creating hash object directory
        std::string hash_dir = file_sha.substr(0, 2);
        std::string hash_obj_file = file_sha.substr(2);
        std::string hash_dir_path = ".git/objects/" + hash_dir;
        std::string hash_obj_path = ".git/objects/" + hash_dir + "/" + hash_obj_file;
        std::filesystem::create_directory(hash_dir_path);

        std::string header = "blob " + std::to_string(file_contents.size()) + '\0';
        std::string object_file = header + file_contents;

        // Creating hash object zlib compressed output file
        zstr::ofstream hash_file (hash_obj_path);
        if (hash_file) {
            hash_file << object_file;
        } else {
            std::cerr << "Failure: Couldn't create hash object file\n";
            return EXIT_FAILURE;
        }
        content_file.close();
    } else {
        std::cerr << "Unknown command " << command << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
