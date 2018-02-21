#ifndef _DISTREF_LB_RULE_HH_
#define _DISTREF_LB_RULE_HH_

// XXX how real flow rules looks like?

/*
 * Current functionality
 * - single pattern matching using subnet mask length
 *
 * TODO:
 * - more complex pattern matching rule using random offset masking
 * - multiple pattern matching with priority
 * - translate range rule to pattern matching
 */

#include <bitset>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "log.hh"
#include "type.hh"

// XXX Optimization by byte array and whole copy
class LBRule {
 private:
  // subnet mask length
  uint8_t sip_mlen;
  uint8_t dip_mlen;
  uint8_t sp_mlen;
  uint8_t dp_mlen;

 public:
  LBRule() {
    this->sip_mlen = 0;
    this->dip_mlen = 0;
    this->sp_mlen = 0;
    this->dp_mlen = 0;
  };

  LBRule(uint8_t sip_mlen, uint8_t dip_mlen, uint8_t sp_mlen, uint8_t dp_mlen) {
    if (sip_mlen > 4 || dip_mlen > 4 || sp_mlen > 2 || dp_mlen > 2) {
      // XXX Raise exception?
      DEBUG_ERR(
          "Cannot create Load Balancing Rule with sum of"
          "mask length exceeds 15 (WorkerID use 15bits)");

      this->sip_mlen = 0;
      this->dip_mlen = 0;
      this->sp_mlen = 0;
      this->dp_mlen = 0;
    }

    this->sip_mlen = sip_mlen;
    this->dip_mlen = dip_mlen;
    this->sp_mlen = sp_mlen;
    this->dp_mlen = dp_mlen;
  };

  std::size_t get_locality_hash(uint32_t sip, uint32_t dip, uint16_t sp,
                                uint16_t dp) const {
    uint32_t hash = 0;

    for (int i = 0; i < sip_mlen; ++i) {
      hash += (sip >> ((3 - i) * 8)) & 0xFF;
      // hash += (sip >> ((3 - i) * 8)) & 0xFF;
      // hash += (hash << 10);
      // hash ^= (hash << 6);
    }

    for (int i = 0; i < dip_mlen; ++i) {
      hash += (dip >> ((3 - i) * 8)) & 0xFF;
      // hash += (dip >> ((3 - i) * 8)) & 0xFF;
      // hash += (hash << 10);
      // hash ^= (hash << 6);
    }

    for (int i = 0; i < sp_mlen; ++i) {
      hash += (sp >> ((1 - i) * 8)) & 0xFF;
      // hash += (hash << 10);
      // hash ^= (hash << 6);
    }

    for (int i = 0; i < dp_mlen; ++i) {
      hash += (dp >> ((1 - i) * 8)) & 0xFF;
      // hash += (hash << 10);
      // hash ^= (hash << 6);
    }

// hash += (hash << 3);
// hash ^= (hash >> 11);
// hash += (hash << 15);
#if 0	
			fprintf(stderr, "%d %d %d %d -> %d %d %d %d : %u\n",
				(sip >> ((3 - 0) * 8)) & 0xFF, 
				(sip >> ((3 - 1) * 8)) & 0xFF, 
				(sip >> ((3 - 2) * 8)) & 0xFF, 
				(sip >> ((3 - 3) * 8)) & 0xFF, 
				(dip >> ((3 - 0) * 8)) & 0xFF, 
				(dip >> ((3 - 1) * 8)) & 0xFF, 
				(dip >> ((3 - 2) * 8)) & 0xFF, 
				(dip >> ((3 - 3) * 8)) & 0xFF,
				hash);
#endif

    return hash;
  }
};

class LBPolicy {
 private:
  LBRule rule;

  // Do not user or change
  LBRule* read_rule_from_string(std::string str_rule) {
    std::size_t dirt_split = str_rule.find("->");
    if (dirt_split == std::string::npos)
      return nullptr;

    std::string src_str = str_rule.substr(0, dirt_split);
    std::string dst_str = str_rule.substr(dirt_split + 2, str_rule.length());

    std::size_t p_split = src_str.find(":");
    if (p_split == std::string::npos) {
      return nullptr;
    }

    int src_ip_mask, src_port_mask, dst_ip_mask, dst_port_mask;
    try {
      src_ip_mask = std::stoi(src_str, nullptr);
      if (src_ip_mask < 0 || src_ip_mask > 32)
        return nullptr;

      src_port_mask =
          std::stoi(src_str.substr(p_split + 1, src_str.length()), nullptr);
      if (src_port_mask < 0 || src_port_mask > 16)
        return nullptr;
    } catch (const std::invalid_argument& ia) {
      return nullptr;
    }

    try {
      p_split = dst_str.find(":");
      if (p_split == std::string::npos) {
        return nullptr;
      }

      dst_ip_mask = std::stoi(dst_str, nullptr);
      if (dst_ip_mask < 0 || dst_ip_mask > 32)
        return nullptr;

      dst_port_mask =
          std::stoi(dst_str.substr(p_split + 1, dst_str.length()), nullptr);
      if (dst_port_mask < 0 || dst_port_mask > 16)
        return nullptr;
    } catch (const std::invalid_argument& ia) {
      return nullptr;
    }

    return new LBRule(src_ip_mask, dst_ip_mask, src_port_mask, dst_port_mask);
  }

 public:
  LBPolicy(){};

  void set_rule(LBRule rule) { this->rule = rule; };

  int set_rule(std::string str_rule) {
    LBRule* rule = read_rule_from_string(str_rule);
    if (!rule) {
      DEBUG_ERR("Wrong load balancing rule format: " << str_rule);
      return -1;
    }
    this->rule = *rule;
    delete rule;

    return 0;
  }

  std::size_t get_locality_hash(uint32_t sip, uint32_t dip, uint16_t sp,
                                uint16_t dp) const {
    return rule.get_locality_hash(sip, dip, sp, dp);
  };
};

#endif
