
#pragma once
#include <map>
#include <random>
#include <string>
#include <vector>

// A single production rule: X -> successor with a given probability
struct LRule {
  char predecessor;
  std::string successor;
  float probability;  // interpreted as a weight; 
};

class LSystem {
 public:
  LSystem();

  // Set the starting string the axiom basically
  void setAxiom(const std::string& axiom);

  // Add a rule: `predecessor` -> `successor` with given probability (default 1.0)
  // You can add multiple rules for the same predecessor (non-deterministic L-system)
  void addRule(char predecessor, const std::string& successor, float probability = 1.0f);

  // Remove all rules
  void clearRules();

  // Generate the final string after `iterations` parallel rewrites
  std::string generate(int iterations) const;

 private:
  std::string applyOnce(const std::string& input) const;

  std::string m_axiom;
  // For each symbol, we store a list of possible rules (for non-determinism)
  std::map<char, std::vector<LRule>> m_rules;

  // RNG is mutable because generation conceptually doesn't change the L-system definition
  mutable std::mt19937 m_rng;
};
