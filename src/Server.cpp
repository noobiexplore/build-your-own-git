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

bool uncompressString(const std::string& compressed, std::string& uncompressed, uLong originalLength) {
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
        return false;
    }

    uncompressed.assign(reinterpret_cast<char*>(uncompressedData), destLen);

    delete [] uncompressedData;
    return true;
}

class GitObject {
public:
    std::string objectSHA;
    std::string objectType;

    GitObject(std::string sha1) : objectSHA(sha1) {}

    bool objectFileToString(std::string& uncompressedObjectString) {
        std::string objectDirectory = objectSHA.substr(0, 2);
        std::string objectFileName = objectSHA.substr(2);
        std::string objectFilePath = ".git/objects/" + objectDirectory + "/" + objectFileName;

        // Uncompressing the object file
        std::string compressedObject = readFileToString(objectFilePath);
        if (compressedObject.empty()) return false;
        // Initila length guess: four times compressed length
        if(!uncompressString(compressedObject, uncompressedObjectString, compressedObject.length() * 4)) {
            return false;
        }

        // Find the type of the object and handle type error
        size_t firstSpace = uncompressedObjectString.find(' ');
        objectType = uncompressedObjectString.substr(0, firstSpace);
        if (objectType != "tree" && objectType != "blob") {
            std::cerr << "fatal error: Invalid object type\n";
            return false;
        }

        return true;
    }
};


class GitCommand {
private:
    int argc;
    std::string command;
    std::string flag;

public:
    GitCommand(std::string command, std::string flag, const int argc)
        : command(command), flag(flag), argc(argc) {}
    GitCommand(std::string command, const int argc)
        : command(command), argc(argc) {}

    bool initGit(void) {
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
                return false;
            }
            std::cout << "Initialized git directory\n";

        } catch (const std::filesystem::filesystem_error& e) {
            std::cout << "Couldn't create directory\ne";
            std::cerr << e.what() << '\n';
            return false;
        }
        return true;
    }

    bool catFile(char* argv[], std::string& catString) {
        if (argc <= 3) {
            std::cerr << "Usage: path/to/your_git.sh cat-file -p <SHA1 hash>" << std::endl;
            return false;
        }

        if (flag != "-p") {
            std::cerr << "Invalid cat-file flag: expected `-p`" << std::endl;
            return false;
        }

        std::string objectSHA = argv[3];
        if (objectSHA.length() != 40) {
            std::cerr << "fatal: Not a valid object name " << objectSHA << ". Expected 40 hex characters\n";
            return false;
        }

        std::string uncompressedObject;
        GitObject gitObject(objectSHA);
        gitObject.objectFileToString(uncompressedObject);

        // Find the NULL character
        size_t nullPos = uncompressedObject.find('\0');
        if (nullPos == std::string::npos) {
            std::cerr << "Invalid git blob format: no null character found\n";
            return false;
        }
        // Separating header from the rest
        catString = uncompressedObject.substr(nullPos + 1);

        return true;
    }

    bool lsTree(char* argv[], std::string& lsTreeString) {
        if (argc < 3) {
            std::cerr << "Usage: path/to/your_git.sh ls-tree(optional) --nameonly <SHA1 hash>\n";
            return false;
        }

        std::string objectSHA = argv[3];
        if (objectSHA.length() != 40) {
            std::cerr << "fatal: Not a valid object name " << objectSHA << ". Expected 40 hex characters\n";
            return false;
        }

        return true;
    }
};



class Blob : public GitObject{

};

class Tree : public GitObject{

};


int main(int argc, char *argv[])
{
    if (argc < 2) {
        std::cerr << "No command provided.\n";
        return EXIT_FAILURE;
    }

    std::string cmd = argv[1];

    std::string flag;
    if (argv[2]) flag = argv[2];

    GitCommand gitCommand(cmd, flag, argc);

    if (cmd == "init") {
        if (!gitCommand.initGit())
            return EXIT_FAILURE;

    } else if (cmd == "cat-file") {
        std::string catString;
        if (!gitCommand.catFile(argv, catString)) {
            return EXIT_FAILURE;
        } else {
            std::cout << catString;
        }

    } else if (cmd == "ls-tree") {
        std::string lsTreeString;
        if (!gitCommand.lsTree(argv, lsTreeString)) {
            return EXIT_FAILURE;
        } else {
            std::cout << lsTreeString;
        }

    } else if (cmd == "hash-object") {
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

        // Write the compressed string to object file
        int st = writeStringToFile(hashObjectPath, compressedObjectContent);
        if (st != 0) {
            std::cerr << "Couldn't open hash-object file for writing\n";
            return EXIT_FAILURE;
        }

    } else {
        std::cerr << "Unknown command " << cmd << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
