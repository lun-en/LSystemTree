//LSystem.cpp
#include "LSystem.h"

#include <numeric>  // std::accumulate

LSystem::LSystem() : m_axiom("") {
    // Seed RNG with non-deterministic seed
    std::random_device rd;
    m_rng = std::mt19937(rd());
}

void LSystem::setSeed(std::uint32_t seed) {
    m_rng.seed(seed);
}

void LSystem::setAxiom(const std::string& axiom) {
    m_axiom = axiom; //starting axiom
}

void LSystem::addRule(char predecessor, const std::string& successor, float probability) {
    if (probability <= 0.0f) {
        // Ignore zero/non-positive probabilities to avoid weird behaviour
        return;
    }
    LRule rule{ predecessor, successor, probability };
    m_rules[predecessor].push_back(rule);
}

void LSystem::clearRules() { m_rules.clear(); }

std::string LSystem::generate(int iterations) const {
    std::string current = m_axiom;
    if (iterations <= 0) {
        return current;
    }

    for (int i = 0; i < iterations; ++i) {
        current = applyOnce(current);
    }
    return current;
}

std::string LSystem::applyOnce(const std::string& input) const {
    std::string output;
    // Reserve a bit more than input size as a heuristic to avoid repeated reallocations
    output.reserve(input.size() * 2);

    for (char c : input) {
        auto it = m_rules.find(c);
        if (it == m_rules.end() || it->second.empty()) {
            // No rule for this symbol: copy it unchanged
            output.push_back(c);
        }
        else {
            const auto& rulesForSymbol = it->second;

            if (rulesForSymbol.size() == 1) {
                // Deterministic: only one possible replacement
                output.append(rulesForSymbol[0].successor);
            }
            else {
                // Non-deterministic: pick one rule based on probabilities
                float totalProb = 0.0f;
                for (const auto& r : rulesForSymbol) {
                    totalProb += r.probability;
                }

                if (totalProb <= 0.0f) {
                    // Fallback: if probabilities are all zero, shouldn't happen, keep symbol
                    output.push_back(c);
                    continue;
                }

                std::uniform_real_distribution<float> dist(0.0f, totalProb);
                float rValue = dist(m_rng);

                float accum = 0.0f;
                const LRule* chosenRule = &rulesForSymbol.back();  // default fallback
                for (const auto& r : rulesForSymbol) {
                    accum += r.probability;
                    if (rValue <= accum) {
                        chosenRule = &r;
                        break;
                    }
                }

                output.append(chosenRule->successor);
            }
        }
    }

    return output;
}
