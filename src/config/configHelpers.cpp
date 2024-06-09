/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   configHelpers.cpp                                  :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: drey <drey@student.42.fr>                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2024/06/01 10:44:20 by doduwole          #+#    #+#             */
/*   Updated: 2024/06/07 23:12:05 by drey             ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../../inc/AllHeaders.hpp"

// clang-format off
std::string
getIndexVariableKey(std::string key,
                    std::map<std::string, std::vector<std::string> > keyValues) {
  typedef std::map<std::string, std::vector<std::string> >::const_iterator
      MapIterator;
  // clang-format on
  std::stringstream finalKey;
  MapIterator it;
  int index = 0;

  for (it = keyValues.begin(); it != keyValues.end(); it++) {
    if (it->first.substr(0, it->first.size() - 3) == key)
      ++index;
  }
  finalKey << "[" << index << "]";
  return finalKey.str();
}

void handleLogFormat(std::string line, std::string &value,
                     std::vector<std::string> tokens,
                     std::vector<std::string>::const_iterator &it) {
  bool firstLine = true;
  std::string cutSemicolon;
  value = "";
  while (true) {
    line = *it;
    tokens = split(line, ' ');
    std::vector<std::string>::iterator token_it = tokens.begin();
    while (token_it != tokens.end()) {
      if (firstLine) {
        value += *(++token_it) + " ";
        firstLine = false;
      } else {
        cutSemicolon = cutTillSemicolon(*(token_it++));
        value += cutSemicolon + " ";
      }
    }
    if (line[line.size() - 1] == ';' || line[line.size() - 1] == '}')
      break;
    it++;
  }
}

void printAllDBData(GroupedDBMap db) {
  GroupedDBMap::const_iterator it;
  for (it = db.begin(); it != db.end(); ++it) {
    std::cout << "Index: " << it->first << std::endl;
    printData(it->second);
    std::cout << "\n" << std::endl;
  }
}

void printData(const std::vector<ConfigDB::KeyMapValue> &values) {
  for (size_t i = 0; i < values.size(); ++i) {
    const MapStr &keyMap = values[i].first;
    const VecStr &valueVector = values[i].second;

    std::cout << "{ " << keyMap.find("directives")->second << ", "
              << keyMap.find("location")->second << " }"
              << ": ";

    for (size_t j = 0; j < valueVector.size(); ++j)
      std::cout << "  " << valueVector[j];

    std::cout << std::endl;
  }
}

std::vector<KeyMapValue> getDataByIdx(GroupedDBMap db, int index) {
  std::vector<KeyMapValue> values;
  GroupedDBMap::iterator it = db.find(index);

  return (it != db.end()) ? it->second
                          : (std::cout << "Index " << index << " does not exist"
                                       << std::endl,
                             values);
}
