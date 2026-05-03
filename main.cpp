#include <pugixml.hpp>
import std;

int main() {
    for (const auto& entry : std::filesystem::directory_iterator("./in")) {
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(entry.path().c_str());
        if (!result) {
            std::println("Failed to load document: {}", entry.path().filename().c_str());
            return -1;
        }


        

        for (pugi::xml_node child : doc.child("protocol").children())  {
            std::println("{} {}", child.name(), child.attribute("name").as_string());
        }
    }
}
