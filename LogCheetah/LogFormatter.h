#pragma once
#include <stdint.h>
#include <ostream>
#include <vector>

const uint32_t LOGFORMAT_RAW = 0;
const uint32_t LOGFORMAT_FIELDS_PSV = 1;
const uint32_t LOGFORMAT_FIELDS_CSV = 2;
const uint32_t LOGFORMAT_FIELDS_TSV = 3;

//logColFilter is ignored if logFormat is LOGFORMAT_RAW
void FormatLogData(uint32_t logFormat, const std::vector<uint32_t> &logRowFilter, const std::vector<uint32_t> &logColFilter, std::ostream &outStream);
