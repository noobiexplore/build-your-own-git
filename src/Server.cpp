#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <zlib.h>
#include "sha1.hpp"

std::string readFileToString(const std::string& filename) {
    std::ifstream file(filename, std::ios::in | std::ios::binary);
    if (!file) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    return buffer.str();
}

int writeStringToFile(const std::string& filename, const std::string& out_str) {
    std::ofstream file(filename, std::ios::out | std::ios::binary);
    if (!file) {
        std::cerr << "Error opening file for writing: " << filename << std::endl;
        return 1;
    }

    file.write(out_str.data(), out_str.size());
    file.close();

    return 0;
}

void compressString(const std::string& uncompressed, std::string& compressed) {
    uLong sourceLen = uncompressed.length();
    uLong destLen = compressBound(sourceLen);

    Bytef* compressedData = new unsigned char[destLen];

    int status = compress(compressedData, &destLen, (Bytef*)(uncompressed.c_str()), sourceLen);

    if (status != Z_OK) {
        std::cerr << "Compression failed with an error code: " << status << std::endl;
        delete [] compressedData;
        return;
    }

    compressed.assign(reinterpret_cast<char*>(compressedData), destLen);

    delete [] compressedData;

    return;
}

void uncompressString(const std::string& compressed, std::string& uncompressed, uLong originalLength) {
    uLong destLen = originalLength;
    Bytef* uncompressedData = new Bytef[destLen];

    int status;
    while ((status = uncompress(uncompressedData, &destLen, (Bytef*)(compressed.c_str()), compressed.length()))
        == Z_BUF_ERROR) {
        delete [] uncompressedData;
        destLen *= 2;
        uncompressedData = new Bytef[destLen];
    }

    if (status != Z_OK) {
        std::cerr << "Decompression Failed with an error code: " << status << std::endl;
        delete [] uncompressedData;
        return;
    }

    uncompressed.assign(reinterpret_cast<char*>(uncompressedData), destLen);

    delete [] uncompressedData;
    return;
}

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

    } else if (command == "cat-file") {
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
        std::string obj_name = ".git/objects/" + obj_dir + "/" + obj_sha;

        // Uncompressing the blob object file
        std::string compressed_blob = readFileToString(obj_name);
        std::string uncompressed_blob;
        // Initila length guess: four times compressed length
        uncompressString(compressed_blob, uncompressed_blob, compressed_blob.length() * 4);
        
        // Find the NULL character
        size_t nullPos = uncompressed_blob.find('\0');
        if (nullPos == std::string::npos) {
            std::cerr << "Invalid git blob format: no null character found\n";
            return EXIT_FAILURE;
        }
        // Separating header from the rest
        std::string blob_content = uncompressed_blob.substr(nullPos + 1);

        std::cout << blob_content;

    } else if (command == "hash-object") {
        if (argc <= 3) {
            std::cerr << "Usage: path/to/your_git hash-object -w <text-file>\n";
            return EXIT_FAILURE;
        }
        std::string hashFlag = argv[2];
        
        if (hashFlag != "-w") {
            std::cerr << "Invalid hash-object flag: expected `-w`\n";
            return EXIT_FAILURE;
        }

        // Read the file content to a string
        std::string fileName = argv[3];
        std::string fileContents = readFileToString(fileName);

        // SHA1 calculation and printing
        SHA1 contentSha;
        contentSha.update(fileContents);
        std::string finalSha = contentSha.final();
        std::cout << finalSha << std::endl;

        // Creating hash object directory
        std::string hashDirectoryName = finalSha.substr(0, 2);
        std::string hashObjectName = finalSha.substr(2);
        std::string hashDirectoryPath = ".git/objects/" + hashDirectoryName;
        std::string hashObjectPath = ".git/objects/" + hashDirectoryName + "/" + hashObjectName;
        std::filesystem::create_directory(hashDirectoryPath);

        // Make the object file: header + content
        std::string header = "blob " + std::to_string(fileContents.size()) + '\0';
        std::string objectContent = header + fileContents;

        // Compress the object file
        std::string compressedObjectContent;
        compressString(objectContent, compressedObjectContent);

        int st = writeStringToFile(hashObjectPath, compressedObjectContent);
        if (st != 0) {
            std::cerr << "Couldn't open hash-object file for writing\n";
            return EXIT_FAILURE;
        }

    } else {
        std::cerr << "Unknown command " << command << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
