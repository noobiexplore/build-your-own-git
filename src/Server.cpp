#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <zlib.h>
#include "sha1.hpp"



std::string readFileToString(const std::string& fileName) {
    std::ifstream file(fileName, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open '" + fileName + 
                                 "' for reading. No such file or directory");
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    file.close();
    return buffer.str();
}

void writeStringToFile(const std::string& fileName, const std::string& out_str) {
    std::ofstream file(fileName, std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open object file for writing: " + fileName);
    }

    file.write(out_str.data(), out_str.size());
    file.close();
}

void compressString(const std::string& uncompressed, std::string& compressed) {
    uLong sourceLen = uncompressed.length();
    uLong destLen = compressBound(sourceLen);

    Bytef* compressedData = new unsigned char[destLen];

    int status = compress(compressedData, &destLen, (Bytef*)(uncompressed.c_str()), sourceLen);

    if (status != Z_OK) {
        delete [] compressedData;
        throw std::runtime_error("Compression failed with error code" + std::to_string(status));
    }

    compressed.assign(reinterpret_cast<char*>(compressedData), destLen);

    delete [] compressedData;
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
        delete [] uncompressedData;
        throw std::runtime_error("Decompression Failed with an error code: " + std::to_string(status));
    }

    uncompressed.assign(reinterpret_cast<char*>(uncompressedData), destLen);

    delete [] uncompressedData;
}

class GitObjectUtility {
public:
    std::string objectSHA;
    std::string objectType;

    GitObjectUtility(std::string sha1) : objectSHA(sha1) {}

    std::string objectFileToString() {
        std::string objectDirectory = objectSHA.substr(0, 2);
        std::string objectFileName = objectSHA.substr(2);
        std::string objectFilePath = ".git/objects/" + objectDirectory + "/" + objectFileName;

        // Uncompressing the object file
        std::string compressedObject;
        try {
            compressedObject = readFileToString(objectFilePath);
        } catch (std::runtime_error& e) {
            throw std::runtime_error("fatal: Invalid object name " + objectSHA + 
                                        ". No such object in .git/object directory");
        } catch (std::exception& e) {
            throw;
        }

        // Initila length guess: four times compressed length
        std::string uncompressedObject;
        try {
            uncompressString(compressedObject, uncompressedObject, compressedObject.length() * 4);
        } catch (std::runtime_error& e) {
            std::cerr << e.what() << std::endl;
            throw;
        } catch (std::exception& e) {
            std::cerr << "Unexpected error: " << e.what() << std::endl;
            throw;
        }

        // Find the type of the object and handle type error
        size_t firstSpace = uncompressedObject.find(' ');
        objectType = uncompressedObject.substr(0, firstSpace);
        if (objectType != "tree" && objectType != "blob")
            throw std::runtime_error("fatal error: Invalid object type");

        return uncompressedObject;
    }

    std::string createObjectDirectory(const std::string objectSHA) {
        // 40 character SHA1 hash. First two characters are object directory name
        // Last 38 character is the object file name
        std::string objectDirName = ".git/objects/" + objectSHA.substr(0, 2);
        std::string objectPath = objectDirName + "/" + objectSHA.substr(2);
    
        try {
            std::filesystem::create_directory(objectDirName);
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Couldn't create directory\n" << e.what() << std::endl;
            throw;
        }

        return objectPath;
    }

    // Raw hex bytes to hex string
    std::string toHex(const uint8_t* buffer, size_t length) {
        std::ostringstream ostream;
        for (size_t i = 0; i < length; i++) {
            ostream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(buffer[i]);
        }
        return ostream.str();

        /* Using C
        for (size_t i = 0; i < length; i++) {
            printf("%02x", (int)buffer[i]);
        }
        printf("\n");
        */
    }
};

class Blob {
private:
    std::string fileName;

public:
    Blob(std::string fileName): fileName(fileName) {}

    void createBlobObject(void) {
        // Read file content
        std::string fileContent;
        try {
            fileContent = readFileToString(fileName);
        } catch (std::runtime_error& e) {
            std::cerr << "fatal: ";
            throw;
        } catch (std::exception& e) {
            throw;
        }

        // Create header and blob object file content
        std::string header = "blob " + std::to_string(fileContent.size()) + '\0';
        std::string blobContent = header + fileContent;

        // SHA1 calculation
        SHA1 hash;
        hash.update(blobContent);
        std::string hashString = hash.final();

        GitObjectUtility gitObjectUtility(hashString);

        // Create object directory
        std::string blobPath;
        try {
            blobPath = gitObjectUtility.createObjectDirectory(hashString);
        } catch (std::filesystem::filesystem_error& e) {
            throw;
        } catch (std::exception& e) {
            throw;
        }

        // Compress the blob file
        std::string compressedBlob;
        try {
            compressString(blobContent, compressedBlob);
        } catch (std::runtime_error& e) {
            throw;
        } catch (std::exception& e) {
            throw;
        }

        // Write the compressed string to object file
        try {
            writeStringToFile(blobPath, compressedBlob);
        } catch (std::runtime_error& e) {
            throw;
        } catch (std::exception& e) {
            throw;
        }

        // Print out the SHA1 hash
        std::cout << hashString << std::endl;
    }
};

class Tree {
private:
    std::string path;

public:
    Tree(std::string path) : path(path) {}

};


class GitCommand {
private:
    int argc;
    std::string command;
    std::string flag;

public:
    GitCommand(std::string command, const int argc)
        : command(command), argc(argc) {}

    void initGit(void) {
        try {
            std::filesystem::create_directory(".git");
            std::filesystem::create_directory(".git/objects");
            std::filesystem::create_directory(".git/refs");
            std::ofstream headFile(".git/HEAD");

            if (headFile.is_open()) {
                headFile << "ref: refs/heads/main\n";
                headFile.close();
            } else {
                throw std::runtime_error("Failed to create .git/HEAD file");
            }
            std::cout << "Initialized git directory\n";

        } catch (const std::filesystem::filesystem_error& e) {
            std::cout << "Couldn't create directory\n";
            std::cerr << e.what() << '\n';
            throw;
        } catch (std::runtime_error& e) {
            std::cerr << e.what() << std::endl;
            throw;
        } catch (std::exception& e) {
            std::cerr << "Unexpected error: " << e.what() << std::endl;
            throw;
        }
    }

    void catFile(char* argv[]) {
        if (argc <= 3)
            throw std::runtime_error("Usage: path/to/your_git.sh cat-file -p <SHA1 hash>");

        flag = argv[2];
        if (flag != "-p")
            throw std::runtime_error("Invalid cat-file flag: expected `-p`");

        std::string objectSHA = argv[3];
        if (objectSHA.length() != 40)
            throw std::runtime_error("fatal: Not a valid object name");

        // Create the object utility structure
        GitObjectUtility gitObjectUtility(objectSHA);
        // Read the object file to a string
        std::string catString;
        try {
            catString = gitObjectUtility.objectFileToString();
        } catch (std::runtime_error& e) {
            throw;
        } catch (std::exception& e) {
            throw;
        }

        // Find the NULL character
        size_t nullPos = catString.find('\0');
        if (nullPos == std::string::npos)
            throw std::runtime_error("Invalid git object format: no null character found");

        // Separating header from the rest
        catString.erase(0, nullPos + 1);
        std::cout << catString;
    }

    void lsTree(char* argv[]) {
        // Argument parsing and error handling
        if (argc < 3)
            throw std::runtime_error("Usage: path/to/your_git.sh ls-tree --name-only(optional) <SHA1 hash>");

        std::string objectSHA;
        if (argc == 4) {
            flag = argv[2];
            objectSHA = argv[3];
        } else if (argc == 3) {
            objectSHA = argv[2];
        } else {
            throw std::runtime_error("Usage: path/to/your_git.sh ls-tree --name-only(optional) <SHA1 hash>");
        }

        if (!flag.empty() && flag != "--name-only")
            throw std::runtime_error("Usage: path/to/your_git.sh ls-tree --name-only(optional) <SHA1 hash>");

        if (objectSHA.length() != 40)
            throw std::runtime_error("fatal: Not a valid object name ");

        // Create the git object utility structure
        GitObjectUtility gitObjectUtility(objectSHA);
        // Read object file to a string
        std::string lsTreeString;
        try {
            lsTreeString = gitObjectUtility.objectFileToString();
        } catch (std::runtime_error& e) {
            throw;
        } catch (std::exception& e) {
            throw;
        }

        if (gitObjectUtility.objectType != "tree")
            throw std::runtime_error("fatal: Not a tree object");

        // Find the NULL character
        size_t nullPos = lsTreeString.find('\0');
        if (nullPos == std::string::npos)
            throw std::runtime_error("Invalid git tree format: no null character found");

        // Separating content from the header
        lsTreeString.erase(0, nullPos + 1);

        // Keep parsing the content of tree file and shortening it
        while (lsTreeString.size() > 0) {
            // Parsing file mode
            size_t firstSpace = lsTreeString.find(' ');
            std::string modeNumber = lsTreeString.substr(0, firstSpace);
            std::string mode;

            if (modeNumber != "40000" && modeNumber != "100755" && modeNumber != "100644") {
                throw std::runtime_error("Invalid object mode number in tree object file");
            } else if (modeNumber == "40000") {
                modeNumber = "040000";
                mode = "tree";
            } else {
                mode = "blob";
            }
            // Dynamically shortening the string
            lsTreeString.erase(0, firstSpace + 1);

            // Parsing file/folder name
            nullPos = lsTreeString.find('\0');
            std::string name = lsTreeString.substr(0, nullPos);
            // Done with upto file name
            lsTreeString.erase(0, nullPos + 1);

            // Now parsing raw 20 byte SHA1 hash of the object
            size_t hexBufferSize = 20;
            uint8_t hexBuffer[hexBufferSize];
            if (lsTreeString.size() < hexBufferSize)
                throw std::runtime_error("Invalid tree object file format");

            std::copy(lsTreeString.begin(), lsTreeString.begin() + hexBufferSize, hexBuffer);
            std::string hexString = gitObjectUtility.toHex(hexBuffer, hexBufferSize);

            // Dealt with one object, onto the next in next iteration
            lsTreeString.erase(0, hexBufferSize);

            // Output object info in a line
            if (flag == "--name-only")
                std::cout << name << std::endl;
            else
                std::cout << modeNumber + " " + mode + " " + hexString + "    " + name << std::endl;
        }
    }

    void hashObject(char* argv[]) {
        if (argc <= 3)
            throw std::runtime_error("Usage: path/to/your_git hash-object -w <file-name>");

        flag = argv[2];
        if (flag != "-w")
            throw std::runtime_error("Invalid hash-object flag: expected `-w`");

        // Read the file name
        std::string fileName = argv[3];

        // Create the blob object
        Blob blobObject(fileName);
        try {
            blobObject.createBlobObject();
        } catch (std::filesystem::filesystem_error& e) {
            throw;
        } catch (std::runtime_error& e) {
            throw;
        } catch (std::exception& e) {
            throw;
        }
    }

    bool writeTree(char* argv[]) {

        return true;
    }
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
        try {
            gitCommand.initGit();
        } catch (std::filesystem::filesystem_error& e) {
            return EXIT_FAILURE;
        } catch (std::runtime_error& e) {
            return EXIT_FAILURE;
        } catch (std::exception& e) {
            return EXIT_FAILURE;
        }

    } else if (cmd == "cat-file") {
        try {
            gitCommand.catFile(argv);
        } catch (std::runtime_error& e) {
            std::cerr << e.what() << std::endl;
            return EXIT_FAILURE;
        } catch (std::exception& e) {
            std::cerr << "Unexpected error: " << e.what() << std::endl;
            return EXIT_FAILURE;
        }

    } else if (cmd == "ls-tree") {
        try {
            gitCommand.lsTree(argv);
        } catch (std::runtime_error& e) {
            std::cerr << e.what() << std::endl;
            return EXIT_FAILURE;
        } catch (std::exception& e) {
            std::cerr << "Unexpected error: " << e.what() << std::endl;
            return EXIT_FAILURE;
        }

    } else if (cmd == "hash-object") {
        try {
            gitCommand.hashObject(argv);
        } catch (std::filesystem::filesystem_error& e) {
            std::cerr << e.what() << std::endl;
            return EXIT_FAILURE;
        } catch (std::runtime_error& e) {
            std::cerr << e.what() << std::endl;
            return EXIT_FAILURE;
        } catch (std::exception& e) {
            std::cerr << "Unexpected error: " << e.what() << std::endl;
            return EXIT_FAILURE;
        }

    } else {
        std::cerr << "Unknown command " << cmd << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
