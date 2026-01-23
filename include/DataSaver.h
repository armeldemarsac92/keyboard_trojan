#ifndef DATASAVER_H
#define DATASAVER_H

#include <cstdint>
#include <string>
#include <vector>



inline std::vector<std::string> stringifyWordMetadata(const WordMetadata& meta) {
    std::vector<std::string> data;

    data.push_back(std::string(meta.word));

    data.push_back(std::to_string(meta.timestamp));

    data.push_back(std::to_string(meta.variance));

    data.push_back(std::to_string(meta.avgInterval));

    data.push_back(std::to_string(meta.entropy));

    return data;
}

void saveToFile(const WordMetadata& data);

#endif