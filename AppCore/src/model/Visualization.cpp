// Port of Visualization.swift: the material library.
#include "sculpt/model/Visualization.hpp"

#include <algorithm>
#include <cctype>
#include <map>

namespace sculpt {

const std::vector<Vec3f>& Appearance::palette() {
  static const std::vector<Vec3f> p = {
      {0.72f, 0.75f, 0.8f}, {0.85f, 0.3f, 0.28f}, {0.95f, 0.6f, 0.2f}, {0.95f, 0.82f, 0.3f}, {0.4f, 0.72f, 0.4f},
      {0.3f, 0.6f, 0.85f},  {0.5f, 0.42f, 0.8f},  {0.25f, 0.26f, 0.3f}, {0.95f, 0.95f, 0.96f},
  };
  return p;
}

const std::vector<Vec3f>& EnvironmentSettings::backgrounds() {
  static const std::vector<Vec3f> b = {
      {0.90f, 0.91f, 0.93f}, {1, 1, 1}, {0.74f, 0.76f, 0.79f}, {0.36f, 0.38f, 0.42f},
      {0.12f, 0.13f, 0.15f}, {0.80f, 0.86f, 0.94f}, {0.93f, 0.89f, 0.82f},
  };
  return b;
}

const std::vector<std::string>& MaterialPreset::families() {
  static const std::vector<std::string> f = {"Plastic", "Metal", "Glass", "Rubber", "Ceramic", "Wood"};
  return f;
}

static MaterialPreset make(const std::string& id, const std::string& family, const std::string& finish,
                           const std::string& name, Vec3f color, float metallic, float roughness, float opacity = 1) {
  Appearance a;
  a.color = color;
  a.metallic = metallic;
  a.roughness = roughness;
  a.opacity = opacity;
  a.preset = id;
  return MaterialPreset{id, family, finish, name, a};
}

const std::vector<MaterialPreset>& MaterialPreset::library() {
  static const std::vector<MaterialPreset> list = [] {
    std::vector<MaterialPreset> l;
    struct P { const char* key; const char* name; Vec3f color; };
    // Plastics: glossy and matte ABS in a range of colours.
    const P plastics[] = {
        {"white", "White", {0.92f, 0.92f, 0.90f}}, {"grey", "Grey", {0.55f, 0.57f, 0.60f}},
        {"black", "Black", {0.06f, 0.06f, 0.07f}}, {"red", "Red", {0.78f, 0.10f, 0.09f}},
        {"orange", "Orange", {0.95f, 0.42f, 0.08f}}, {"yellow", "Yellow", {0.96f, 0.78f, 0.12f}},
        {"green", "Green", {0.18f, 0.58f, 0.26f}}, {"blue", "Blue", {0.10f, 0.32f, 0.78f}},
    };
    for (auto& p : plastics)
      l.push_back(make(std::string("plastic.glossy.abs.") + p.key, "Plastic", "Glossy", std::string(p.name) + " ABS",
                       p.color, 0, 0.18f));
    for (auto& p : plastics) {
      std::string k = p.key;
      if (k == "white" || k == "grey" || k == "black" || k == "red" || k == "blue")
        l.push_back(make("plastic.matte.abs." + k, "Plastic", "Matte", std::string(p.name) + " ABS", p.color, 0, 0.75f));
    }
    // Metals: base colour is the reflectance (F0) of the metal.
    l.push_back(make("metal.polished.iron", "Metal", "Polished", "Iron", {0.56f, 0.57f, 0.58f}, 1, 0.15f));
    l.push_back(make("metal.brushed.iron", "Metal", "Brushed", "Iron", {0.53f, 0.54f, 0.55f}, 1, 0.45f));
    l.push_back(make("metal.polished.steel", "Metal", "Polished", "Steel", {0.66f, 0.67f, 0.69f}, 1, 0.12f));
    l.push_back(make("metal.brushed.steel", "Metal", "Brushed", "Steel", {0.62f, 0.63f, 0.65f}, 1, 0.4f));
    l.push_back(make("metal.polished.aluminium", "Metal", "Polished", "Aluminium", {0.91f, 0.92f, 0.92f}, 1, 0.14f));
    l.push_back(make("metal.brushed.aluminium", "Metal", "Brushed", "Aluminium", {0.86f, 0.87f, 0.88f}, 1, 0.42f));
    l.push_back(make("metal.anodized.aluminium.black", "Metal", "Anodized", "Black Aluminium", {0.10f, 0.10f, 0.11f},
                     0.85f, 0.38f));
    l.push_back(make("metal.polished.gold", "Metal", "Polished", "Gold", {1.0f, 0.77f, 0.34f}, 1, 0.12f));
    l.push_back(make("metal.polished.copper", "Metal", "Polished", "Copper", {0.95f, 0.64f, 0.54f}, 1, 0.15f));
    l.push_back(make("metal.brushed.copper", "Metal", "Brushed", "Copper", {0.90f, 0.60f, 0.50f}, 1, 0.42f));
    l.push_back(make("metal.polished.brass", "Metal", "Polished", "Brass", {0.91f, 0.78f, 0.42f}, 1, 0.16f));
    l.push_back(make("metal.polished.chrome", "Metal", "Polished", "Chrome", {0.95f, 0.95f, 0.96f}, 1, 0.04f));

    l.push_back(make("glass.clear", "Glass", "Clear", "Glass", {0.92f, 0.95f, 0.96f}, 0, 0.04f, 0.18f));
    l.push_back(make("glass.frosted", "Glass", "Frosted", "Glass", {0.90f, 0.93f, 0.95f}, 0, 0.5f, 0.55f));
    l.push_back(make("glass.tinted.blue", "Glass", "Tinted", "Blue Glass", {0.35f, 0.55f, 0.80f}, 0, 0.05f, 0.35f));
    l.push_back(make("glass.tinted.smoke", "Glass", "Tinted", "Smoke Glass", {0.22f, 0.23f, 0.25f}, 0, 0.05f, 0.45f));
    l.push_back(make("glass.tinted.green", "Glass", "Tinted", "Green Glass", {0.40f, 0.68f, 0.48f}, 0, 0.05f, 0.35f));

    l.push_back(make("rubber.black", "Rubber", "Matte", "Black Rubber", {0.05f, 0.05f, 0.055f}, 0, 0.9f));
    l.push_back(make("rubber.grey", "Rubber", "Matte", "Grey Rubber", {0.32f, 0.33f, 0.35f}, 0, 0.88f));
    l.push_back(make("rubber.red", "Rubber", "Matte", "Red Rubber", {0.60f, 0.10f, 0.08f}, 0, 0.85f));

    l.push_back(make("ceramic.glossy.white", "Ceramic", "Glossy", "White Ceramic", {0.94f, 0.93f, 0.90f}, 0, 0.08f));
    l.push_back(make("ceramic.matte.white", "Ceramic", "Matte", "White Ceramic", {0.90f, 0.89f, 0.86f}, 0, 0.6f));
    l.push_back(make("ceramic.glossy.terracotta", "Ceramic", "Glossy", "Terracotta", {0.72f, 0.36f, 0.22f}, 0, 0.25f));

    l.push_back(make("wood.oak", "Wood", "Satin", "Oak", {0.70f, 0.52f, 0.32f}, 0, 0.55f));
    l.push_back(make("wood.walnut", "Wood", "Satin", "Walnut", {0.36f, 0.23f, 0.14f}, 0, 0.5f));
    l.push_back(make("wood.maple", "Wood", "Satin", "Maple", {0.86f, 0.72f, 0.52f}, 0, 0.55f));
    l.push_back(make("wood.cherry", "Wood", "Varnished", "Cherry", {0.58f, 0.28f, 0.16f}, 0, 0.3f));
    return l;
  }();
  return list;
}

const MaterialPreset* MaterialPreset::preset(const std::string& id) {
  static const std::map<std::string, const MaterialPreset*> byId = [] {
    std::map<std::string, const MaterialPreset*> m;
    for (auto& p : library()) m[p.id] = &p;
    return m;
  }();
  auto it = byId.find(id);
  return it == byId.end() ? nullptr : it->second;
}

std::vector<MaterialPreset> MaterialPreset::presets(const std::string& family) {
  std::vector<MaterialPreset> out;
  for (auto& p : library())
    if (p.family == family) out.push_back(p);
  return out;
}

std::pair<std::string, std::string> MaterialPreset::label(const Appearance& a) {
  if (a.preset)
    if (auto p = preset(*a.preset)) return {p->finish, p->name};
  if (a == Appearance()) return {"Default", "Material"};
  std::string finish = a.opacity < 1                ? "Translucent"
                       : a.metallic > 0.5f          ? (a.roughness < 0.3f ? "Polished" : "Brushed")
                       : a.roughness < 0.3f         ? "Glossy"
                       : a.roughness > 0.7f         ? "Matte"
                                                    : "Satin";
  return {finish, a.metallic > 0.5f ? "Custom Metal" : "Custom"};
}

static std::string lower(std::string s) {
  for (auto& c : s) c = char(std::tolower((unsigned char)c));
  return s;
}

std::vector<MaterialPreset> MaterialPreset::search(const std::string& query) {
  std::string q = query;
  while (!q.empty() && q.front() == ' ') q.erase(q.begin());
  while (!q.empty() && q.back() == ' ') q.pop_back();
  q = lower(q);
  if (q.empty()) return library();
  std::vector<MaterialPreset> out;
  for (auto& p : library())
    if (lower(p.family + " " + p.finish + " " + p.name).find(q) != std::string::npos) out.push_back(p);
  return out;
}

}  // namespace sculpt
