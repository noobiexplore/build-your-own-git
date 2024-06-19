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

    bool objectFileToString(std::string& uncompressedObject, std::string& objectType) {
        std::string objectDirectory = objectSHA.substr(0, 2);
        std::string objectFileName = objectSHA.substr(2);
        std::string objectFilePath = ".git/objects/" + objectDirectory + "/" + objectFileName;

        // Uncompressing the object file
        std::string compressedObject = readFileToString(objectFilePath);
        if (compressedObject.empty()) return false;
        // Initila length guess: four times compressed length
        if(!uncompressString(compressedObject, uncompressedObject, compressedObject.length() * 4)) {
            return false;
        }

        // Find the type of the object and handle type error
        size_t firstSpace = uncompressedObject.find(' ');
        objectType = uncompressedObject.substr(0, firstSpace);
        this->objectType = objectType;
        if (objectType != "tree" && objectType != "blob") {
            std::cerr << "fatal error: Invalid object type\n";
            return false;
        }

        return true;
    }

    // Raw hex bytes to hex string
    std::string toHex(const uint8_t* buffer, size_t length) {
        std::ostringstream ostream;
        for (size_t i = 0; i < length; i++) {
            ostream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(buffer[i]);
        }
        return ostream.str();

        /*
        for (size_t i = 0; i < length; i++) {
            printf("%02x", (int)buffer[i]);
        }
        printf("\n");
        */
    }
};


class GitCommand {
private:
    int argc;
    std::string command;
    std::string flag;

public:
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

        flag = argv[2];
        if (flag != "-p") {
            std::cerr << "Invalid cat-file flag: expected `-p`" << std::endl;
            return false;
        }

        std::string objectSHA = argv[3];
        if (objectSHA.length() != 40) {
            std::cerr << "fatal: Not a valid object name " << objectSHA << ". Expected 40 hex characters\n";
            return false;
        }

        std::string uncompressedObject, objectType;
        GitObject gitObject(objectSHA);
        gitObject.objectFileToString(uncompressedObject, objectType);

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
            std::cerr << "Usage: path/to/your_git.sh ls-tree --nameonly(optional) <SHA1 hash>\n";
            return false;
        }

        std::string objectSHA;
        if (argc == 4) {
            flag = argv[2];
            objectSHA = argv[3];
        } else if (argc == 3) {
            objectSHA = argv[2];
        } else {
            std::cerr << "Usage: path/to/your_git.sh ls-tree --nameonly(optional) <SHA1 hash>\n";
            return false;
        }

        if (objectSHA.length() != 40) {
            std::cerr << "fatal: Not a valid object name " << objectSHA << ". Expected 40 hex characters\n";
            return false;
        }

        std::string uncompressedObject, objectType;
        GitObject gitObject(objectSHA);
        gitObject.objectFileToString(uncompressedObject, objectType);

        if (objectType != "tree") {
            std::cerr << "fatal: Not a tree object\n";
            return false;
        }

        // Find the NULL character
        size_t nullPos = uncompressedObject.find('\0');
        if (nullPos == std::string::npos) {
            std::cerr << "Invalid git tree format: no null character found\n";
            return false;
        }
        // Separating header from the rest
        lsTreeString = uncompressedObject.substr(nullPos + 1);
        while (lsTreeString.size() > 0) {
            // Parsing file mode
            size_t firstSpace = lsTreeString.find(' ');
            std::string modeNumber = lsTreeString.substr(0, firstSpace);
            std::string mode;

            if (modeNumber != "40000" && modeNumber != "100755") {
                std::cerr << "Invalid object mode number in tree object file\n";
                return false;
            } else if (modeNumber == "40000") {
                modeNumber = "040000";
                mode = "tree";
            } else {
                mode = "blob";
            }
            // Done with upto file mode
            lsTreeString = lsTreeString.substr(firstSpace+1);

            // Parsing file/folder name
            nullPos = lsTreeString.find('\0');
            std::string name = lsTreeString.substr(0, nullPos);
            // Done with upto file name
            lsTreeString = lsTreeString.substr(nullPos+1);

            // Now parsing raw 20 byte SHA1 hash of the object
            size_t hexBufferSize = 20;
            uint8_t hexBuffer[hexBufferSize];
            if (lsTreeString.size() < hexBufferSize) {
                std::cerr << "Invalid tree object file format\n";
                return false;
            }
            std::copy(lsTreeString.begin(), lsTreeString.begin() + hexBufferSize, hexBuffer);
            std::string hexString = gitObject.toHex(hexBuffer, hexBufferSize);

            // Dealt with one object, onto the next in next iteration
            lsTreeString = lsTreeString.substr(hexBufferSize);

            // Output object info in a line
            if (!flag.empty() && flag != "--name-only") {
                std::cerr << "Usage: path/to/your_git.sh ls-tree --nameonly(optional) <SHA1 hash>\n";
                return false;
            } else if (flag == "--name-only") {
                std::cout << name << std::endl;
            } else {
                std::cout << modeNumber + " " + mode + " " + hexString + "    " + name << std::endl;
            }
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
    GitCommand gitCommand(cmd, argc);

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
