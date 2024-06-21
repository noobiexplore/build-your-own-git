#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <zlib.h>
#include "sha1.hpp"



bool readFileToString(const std::string& filename, std::string& fileString) {
    std::ifstream file(filename, std::ios::in | std::ios::binary);
    if (!file) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    fileString =  buffer.str();

    return true;
}

bool writeStringToFile(const std::string& filename, const std::string& out_str) {
    std::ofstream file(filename, std::ios::out | std::ios::binary);
    if (!file) {
        std::cerr << "Error opening object file for writing: " << filename << std::endl;
        return false;
    }

    file.write(out_str.data(), out_str.size());
    file.close();

    return true;
}

bool compressString(const std::string& uncompressed, std::string& compressed) {
    uLong sourceLen = uncompressed.length();
    uLong destLen = compressBound(sourceLen);

    Bytef* compressedData = new unsigned char[destLen];

    int status = compress(compressedData, &destLen, (Bytef*)(uncompressed.c_str()), sourceLen);

    if (status != Z_OK) {
        std::cerr << "Compression failed with an error code: " << status << std::endl;
        delete [] compressedData;
        return false;
    }

    compressed.assign(reinterpret_cast<char*>(compressedData), destLen);

    delete [] compressedData;

    return true;
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

class GitObjectUtil {
public:
    std::string objectSHA;
    std::string objectType;

    GitObjectUtil(std::string sha1) : objectSHA(sha1) {}

    bool objectFileToString(std::string& uncompressedObject, std::string& objectType) {
        std::string objectDirectory = objectSHA.substr(0, 2);
        std::string objectFileName = objectSHA.substr(2);
        std::string objectFilePath = ".git/objects/" + objectDirectory + "/" + objectFileName;

        // Uncompressing the object file
        std::string compressedObject;
        if(!readFileToString(objectFilePath, compressedObject)) return false;
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

    bool createObjectDirectory(std::string& objectName) {
        // 40 character SHA1 hash. First two characters are object directory name
        // Last 38 character is the object file name
        std::string objectDirName = ".git/objects/" + objectSHA.substr(0, 2);
        objectName = objectDirName + "/" + objectSHA.substr(2);
    
        try {
            std::filesystem::create_directory(objectDirName);
        } catch (const std::filesystem::filesystem_error& e) {
            std::cout << "Couldn't create directory\n";
            std::cerr << e.what() << '\n';
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

    bool createBlobObject(void) {
        std::string fileContent;
        if(!readFileToString(fileName, fileContent)) 
            return false;

        // Create header and blob file
        std::string header = "blob " + std::to_string(fileContent.size()) + '\0';
        std::string blobContent = header + fileContent;

        // SHA1 calculation
        SHA1 hash;
        hash.update(blobContent);
        std::string hashString = hash.final();

        GitObjectUtil gitObject(hashString);

        // Create object directory
        std::string blobName;
        if (!gitObject.createObjectDirectory(blobName)) 
            return false;


        // Compress the blob file
        std::string compressedBlob;
        if (!compressString(blobContent, compressedBlob)) 
            return false;

        // Write the compressed string to object file
        if (!writeStringToFile(blobName, compressedBlob)) 
            return false;

        // Print out the SHA1 hash
        std::cout << hashString << std::endl;

        return true;
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

    bool catFile(char* argv[]) {
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

        std::string catString, objectType;
        GitObjectUtil gitObject(objectSHA);
        if (!gitObject.objectFileToString(catString, objectType))
            return false;

        // Find the NULL character
        size_t nullPos = catString.find('\0');
        if (nullPos == std::string::npos) {
            std::cerr << "Invalid git object format: no null character found\n";
            return false;
        }
        // Separating header from the rest
        catString.erase(0, nullPos + 1);
        std::cout << catString;

        return true;
    }

    bool lsTree(char* argv[]) {
        // Argument parsing and error handling
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


        if (!flag.empty() && flag != "--name-only") {
            std::cerr << "Usage: path/to/your_git.sh ls-tree --nameonly(optional) <SHA1 hash>\n";
            return false;
        }

        if (objectSHA.length() != 40) {
            std::cerr << "fatal: Not a valid object name " << objectSHA << ". Expected 40 hex characters\n";
            return false;
        }

        // Read object file to a string
        std::string lsTreeString, objectType;
        GitObjectUtil gitObject(objectSHA);
        gitObject.objectFileToString(lsTreeString, objectType);

        if (objectType != "tree") {
            std::cerr << "fatal: Not a tree object\n";
            return false;
        }

        // Find the NULL character
        size_t nullPos = lsTreeString.find('\0');
        if (nullPos == std::string::npos) {
            std::cerr << "Invalid git tree format: no null character found\n";
            return false;
        }

        // Separating content from the header
        lsTreeString.erase(0, nullPos + 1);

        // Keep parsing the content of tree file and shortening it
        while (lsTreeString.size() > 0) {
            // Parsing file mode
            size_t firstSpace = lsTreeString.find(' ');
            std::string modeNumber = lsTreeString.substr(0, firstSpace);
            std::string mode;

            if (modeNumber != "40000" && modeNumber != "100755" && modeNumber != "100644") {
                std::cerr << "Invalid object mode number in tree object file\n";
                return false;
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
            if (lsTreeString.size() < hexBufferSize) {
                std::cerr << "Invalid tree object file format\n";
                return false;
            }
            std::copy(lsTreeString.begin(), lsTreeString.begin() + hexBufferSize, hexBuffer);
            std::string hexString = gitObject.toHex(hexBuffer, hexBufferSize);

            // Dealt with one object, onto the next in next iteration
            lsTreeString.erase(0, hexBufferSize);

            // Output object info in a line
            if (flag == "--name-only") {
                std::cout << name << std::endl;
            } else {
                std::cout << modeNumber + " " + mode + " " + hexString + "    " + name << std::endl;
            }
        }

        return true;
    }

    bool hashObject(char* argv[]) {
        if (argc <= 3) {
            std::cerr << "Usage: path/to/your_git hash-object -w <file-name>\n";
            return false;
        }

        flag = argv[2];
        if (flag != "-w") {
            std::cerr << "Invalid hash-object flag: expected `-w`\n";
            return false;
        }

        // Read the file name
        std::string fileName = argv[3];

        // Create the blob object
        Blob blobObject(fileName);
        if (!blobObject.createBlobObject()) return false;

        return true;
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
        if (!gitCommand.initGit())
            return EXIT_FAILURE;

    } else if (cmd == "cat-file") {
        if (!gitCommand.catFile(argv))
            return EXIT_FAILURE;

    } else if (cmd == "ls-tree") {
        if (!gitCommand.lsTree(argv))
            return EXIT_FAILURE;

    } else if (cmd == "hash-object") {
        if (!gitCommand.hashObject(argv))
            return EXIT_FAILURE;

    } else {
        std::cerr << "Unknown command " << cmd << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
