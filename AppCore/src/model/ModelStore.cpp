// Port of ModelStore.swift: history, replay, drafts, selection, sketch
// sessions and exchange.
#include "sculpt/model/ModelStore.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "sculpt/model/DisplayFormat.hpp"
#include "sculpt/model/Expression.hpp"

namespace fs = std::filesystem;

namespace sculpt {

std::string MeasureRow::text() const { return formatted(value) + " " + unit; }

ModelStore::ModelStore(std::vector<Feature> features, ProjectSettings settings)
    : kernel_(std::make_unique<o3d::Kernel>()), features_(std::move(features)), settings_(std::move(settings)) {
  regenerate();
}

void ModelStore::changed() {
  ++revision_;
  if (onChanged) onChanged();
}

// MARK: Setters

void ModelStore::setDraft(std::optional<Draft> d) {
  draft_ = std::move(d);
  changed();
}

void ModelStore::setSelection(std::vector<Pick> s) {
  selection_ = std::move(s);
  changed();
}

void ModelStore::setSketch(std::optional<SketchSession> s) {
  std::optional<SketchSession> old = std::move(sketch_);
  sketch_ = std::move(s);
  if (!sketch_) {
    sketchUndo_.clear();
    sketchRedo_.clear();
  } else if (old && old->sketch != sketch_->sketch && !old->dragging && !restoringSketch_) {
    // Every geometry change is one undo step, except the steps of a drag,
    // which beginSketchDrag records once up front.
    sketchUndo_.push_back(old->sketch);
    sketchRedo_.clear();
  }
  changed();
}

void ModelStore::updateSketch(const std::function<void(SketchSession&)>& change) {
  if (!sketch_) return;
  SketchSession s = *sketch_;
  change(s);
  setSketch(std::move(s));
}

void ModelStore::setToast(std::optional<std::string> t) {
  toast_ = std::move(t);
  changed();
}

void ModelStore::setDocumentName(std::string n) {
  documentName_ = std::move(n);
  changed();
}

void ModelStore::setDocumentPath(std::string p) {
  documentPath_ = std::move(p);
  changed();
}

void ModelStore::setIsolationRestore(std::optional<std::set<int>> r) {
  isolationRestore_ = std::move(r);
  changed();
}

void ModelStore::setItemSelection(std::vector<Uuid> s) {
  itemSelection_ = std::move(s);
  changed();
}

void ModelStore::setSettings(ProjectSettings s) {
  if (s == settings_) return;
  ProjectSettings old = std::move(settings_);
  settings_ = std::move(s);
  if (onSettingsChanged) onSettingsChanged(settings_);
  if (settings_.variables != old.variables) {
    regenerate();
  } else {
    ++geometryVersion_;
    changed();
  }
}

void ModelStore::updateSettings(const std::function<void(ProjectSettings&)>& change) {
  ProjectSettings s = settings_;
  change(s);
  setSettings(std::move(s));
}

void ModelStore::reload(std::vector<Feature> features, ProjectSettings settings) {
  undoStack_.clear();
  redoStack_.clear();
  draft_.reset();
  features_ = std::move(features);
  setSettings(std::move(settings));
  regenerate();
}

std::vector<int> ModelStore::bodyIds() const {
  std::vector<int> ids;
  for (auto& [id, _] : meshes_) ids.push_back(id);
  return ids;
}

std::optional<std::string> ModelStore::draftError() const {
  if (!draft_) return std::nullopt;
  auto it = errors_.find(draft_->feature.id);
  if (it == errors_.end()) return std::nullopt;
  return it->second;
}

// MARK: History

void ModelStore::commit(std::vector<Feature> newFeatures) {
  undoStack_.push_back(features_);
  redoStack_.clear();
  features_ = std::move(newFeatures);
  if (onFeaturesChanged) onFeaturesChanged(features_);
  regenerate();
}

void ModelStore::undo() {
  if (undoStack_.empty()) return;
  redoStack_.push_back(features_);
  features_ = std::move(undoStack_.back());
  undoStack_.pop_back();
  draft_.reset();
  if (onFeaturesChanged) onFeaturesChanged(features_);
  regenerate();
}

void ModelStore::redo() {
  if (redoStack_.empty()) return;
  undoStack_.push_back(features_);
  features_ = std::move(redoStack_.back());
  redoStack_.pop_back();
  draft_.reset();
  if (onFeaturesChanged) onFeaturesChanged(features_);
  regenerate();
}

void ModelStore::toggleSuppressed(const Uuid& id) {
  auto list = features_;
  for (auto& f : list)
    if (f.id == id) f.suppressed = !f.suppressed;
  commit(std::move(list));
}

void ModelStore::deleteFeature(const Uuid& id) {
  std::vector<Feature> list;
  for (auto& f : features_)
    if (f.id != id) list.push_back(f);
  commit(std::move(list));
}

void ModelStore::regenerate() {
  draftRebuildPending_ = false;
  std::vector<Feature> list = features_;
  // While a feature's sketch is edited, show the model as it was before it.
  if (sketch_ && sketch_->replacing) {
    auto it = std::find_if(list.begin(), list.end(), [&](auto& f) { return f.id == *sketch_->replacing; });
    if (it != list.end()) list.erase(it, list.end());
  }
  if (draft_) {
    auto it = draft_->replacing ? std::find_if(list.begin(), list.end(), [&](auto& f) { return f.id == *draft_->replacing; })
                                : list.end();
    if (it != list.end()) *it = draft_->feature;
    else list.push_back(draft_->feature);
  }
  std::map<Uuid, std::string> newErrors;
  std::vector<ReferenceItem> items;
  replaySketches_.clear();
  scatterOrigins_.clear();
  ExpressionEvaluator evaluator(settings_.variables);
  std::vector<Feature> resolved;
  resolved.reserve(list.size());
  for (auto& f : list) {
    Feature g = f;
    g.op = f.resolved(evaluator);
    resolved.push_back(std::move(g));
  }
  size_t start = 0;
  while (start < std::min(resolved.size(), checkpoints_.size()) && checkpoints_[start].feature == resolved[start]) ++start;
  if (start > 0) {
    const auto& c = checkpoints_[start - 1];
    kernel_->restoreState(c.state);
    replaySketches_ = c.sketches;
    items = c.items;
    newErrors = c.errors;
    scatterOrigins_ = c.origins;
  } else {
    kernel_->reset();
  }
  checkpoints_.erase(checkpoints_.begin() + long(start), checkpoints_.end());
  for (size_t k = start; k < resolved.size(); ++k) {
    const Feature& f = resolved[k];
    if (!f.suppressed) {
      const Operation& op = f.op;
      if (auto o = op.as<op::SketchItem>()) {
        replaySketches_[f.id] = o->sketch;
        items.push_back({f.id, o->name, o->sketch});
      } else if (auto o = op.as<op::Plane>()) {
        items.push_back({f.id, o->name, o->base.transformed(o->offset, o->angle)});
      } else if (auto o = op.as<op::Axis>()) {
        items.push_back({f.id, o->name, ReferenceItem::Axis{o->origin, o->direction}});
      } else if (auto o = op.as<op::Image>()) {
        items.push_back({f.id, o->image.name, o->image});
      } else if (auto o = op.as<op::Projection>()) {
        // Edges as they are at this point in the history, as sketch lines.
        Sketch sketch(o->plane);
        for (auto& line : kernel_->projectEdges(o->body, o->plane.bridged())) {
          std::vector<int> ids;
          for (auto& p : line) ids.push_back(sketch.addPoint(fromArr(p)));
          for (size_t i = 0; i + 1 < ids.size(); ++i) sketch.addLine(ids[i], ids[i + 1]);
        }
        if (sketch.isEmpty()) newErrors[f.id] = "Nothing to project";
        replaySketches_[f.id] = sketch;
        items.push_back({f.id, o->name, sketch});
      } else if (auto error = apply(op)) {
        newErrors[f.id] = *error;
      }
    }
    checkpoints_.push_back(Checkpoint{f, kernel_->saveState(), replaySketches_, items, newErrors, scatterOrigins_});
  }
  errors_ = std::move(newErrors);
  referenceItems_ = std::move(items);
  itemSelection_.erase(std::remove_if(itemSelection_.begin(), itemSelection_.end(),
                                      [&](const Uuid& id) {
                                        return std::none_of(referenceItems_.begin(), referenceItems_.end(),
                                                            [&](auto& i) { return i.id == id; });
                                      }),
                       itemSelection_.end());

  std::map<int, std::shared_ptr<const BodyMesh>> newMeshes;
  std::map<int, uint64_t> keys;
  for (int id : kernel_->bodies()) {
    uint64_t key = kernel_->shapeKey(id);
    keys[id] = key;
    auto old = meshes_.find(id);
    auto oldKey = meshKeys_.find(id);
    if (key != 0 && oldKey != meshKeys_.end() && oldKey->second == key && old != meshes_.end()) {
      newMeshes[id] = old->second;
    } else {
      o3d::Mesh mesh;
      if (kernel_->tessellate(id, mesh, 1)) {
        auto m = std::make_shared<BodyMesh>(id, mesh);
        m->shapeKey = key;
        newMeshes[id] = m;
      }
    }
  }
  meshKeys_ = keys;
  meshState_ = kernel_->saveState();
  // Only keys of live shapes (kept alive by meshState) stay, so a freed
  // shape's key can't be mistaken for a new one.
  for (auto it = propertiesCache_.begin(); it != propertiesCache_.end();) {
    auto k = keys.find(it->first);
    if (k != keys.end() && k->second == it->second.first) ++it;
    else it = propertiesCache_.erase(it);
  }
  meshes_ = std::move(newMeshes);
  // Presentation is keyed by body id; forget ids the settled history no
  // longer has, so a later body with the same id starts plain.
  if (!draft_ && !sketch_) {
    auto live = [&](int id) { return meshes_.count(id) > 0; };
    ProjectSettings pruned = settings_;
    for (auto it = pruned.bodyNames.begin(); it != pruned.bodyNames.end();)
      it = live(it->first) ? std::next(it) : pruned.bodyNames.erase(it);
    for (auto& [id, origin] : scatterOrigins_) {
      if (pruned.appearance.count(id)) continue;
      auto a = pruned.appearance.find(origin.first);
      if (a == pruned.appearance.end()) a = pruned.appearance.find(origin.second);
      if (a != pruned.appearance.end()) {
        Appearance look = a->second;
        pruned.appearance[id] = look;
      }
    }
    for (auto it = pruned.appearance.begin(); it != pruned.appearance.end();)
      it = live(it->first) ? std::next(it) : pruned.appearance.erase(it);
    for (auto it = pruned.hiddenBodies.begin(); it != pruned.hiddenBodies.end();)
      it = live(*it) ? std::next(it) : pruned.hiddenBodies.erase(it);
    if (pruned != settings_) setSettings(std::move(pruned));
  }
  selection_.erase(std::remove_if(selection_.begin(), selection_.end(),
                                  [&](const Pick& pick) {
                                    auto m = meshes_.find(pick.body);
                                    if (m == meshes_.end()) return true;
                                    switch (pick.kind) {
                                      case Pick::Kind::Body: return false;
                                      case Pick::Kind::Face: return !(pick.index < m->second->faceCount);
                                      case Pick::Kind::Edge: return !(pick.index < m->second->edgeCount);
                                    }
                                    return false;
                                  }),
                   selection_.end());
  hover_.reset();
  ++geometryVersion_;
  changed();
}

std::optional<std::string> ModelStore::apply(const Operation& op) {
  o3d::Kernel& k = *kernel_;
  bool ok = false;
  auto ints = [](const std::vector<int>& v) { return v; };
  if (auto o = op.as<op::Primitive>()) {
    ok = k.primitive(bridged(o->kind), {o->dims.x, o->dims.y, o->dims.z}, toO3d(o->position)) > 0;
  } else if (auto o = op.as<op::Extrude>()) {
    // A cut's distance is its depth into the material, against the sketch normal.
    ok = k.extrude(o->sketch.plane.bridged(), o->sketch.profiles(), o->mode == CombineMode::cut ? -o->distance : o->distance,
                   bridged(o->mode), o->target.value_or(-1)) > 0;
  } else if (auto o = op.as<op::Revolve>()) {
    ok = k.revolve(o->sketch.plane.bridged(), o->sketch.profiles(), o->aroundV, o->angle, bridged(o->mode),
                   o->target.value_or(-1)) > 0;
  } else if (auto o = op.as<op::PushPull>()) {
    ok = k.pushPull(o->body, o->face, o->distance);
  } else if (auto o = op.as<op::Boolean>()) {
    ok = k.boolean(o->target, o->tool, bridged(o->kind));
  } else if (auto o = op.as<op::Fillet>()) {
    ok = k.fillet(o->body, ints(o->edges), o->radius);
  } else if (auto o = op.as<op::Chamfer>()) {
    ok = k.chamfer(o->body, ints(o->edges), o->distance);
  } else if (auto o = op.as<op::Shell>()) {
    ok = k.shell(o->body, ints(o->faces), o->thickness);
  } else if (auto o = op.as<op::Move>()) {
    ok = k.transform(o->body, toO3d(o->translation), toO3d(o->axis), o->angle);
  } else if (auto o = op.as<op::Mirror>()) {
    ok = k.mirror(o->body, o->plane.bridged(), o->keepOriginal) > 0;
  } else if (auto o = op.as<op::Duplicate>()) {
    ok = k.duplicate(o->body) > 0;
  } else if (auto o = op.as<op::Delete>()) {
    ok = k.remove(o->body);
  } else if (auto o = op.as<op::Imported>()) {
    ok = k.fromBrep(o->brep) > 0;
  } else if (op.is<op::SketchItem>() || op.is<op::Plane>() || op.is<op::Axis>() || op.is<op::Image>() ||
             op.is<op::Projection>()) {
    ok = true;
  } else if (auto o = op.as<op::Translate>()) {
    ok = k.transform(o->body, toO3d(normalize(o->direction) * o->distance), {0, 0, 1}, 0);
  } else if (auto o = op.as<op::Rotate>()) {
    ok = k.rotate(o->body, toO3d(o->origin), toO3d(o->axis), o->angle);
  } else if (auto o = op.as<op::Align>()) {
    o3d::Properties a, b;
    if (!k.properties(o->body, a) || !k.properties(o->target, b)) return std::string("Align needs two bodies");
    Vec3d amin = fromO3d(a.bboxMin), amax = fromO3d(a.bboxMax), bmin = fromO3d(b.bboxMin), bmax = fromO3d(b.bboxMax);
    Vec3d t;
    for (int i = 0; i < 3 && i < int(o->modes.size()); ++i) {
      switch (o->modes[size_t(i)]) {
        case 1: t[i] = bmin[i] - amin[i]; break;
        case 2: t[i] = (bmin[i] + bmax[i]) / 2 - (amin[i] + amax[i]) / 2; break;
        case 3: t[i] = bmax[i] - amax[i]; break;
        default: break;
      }
    }
    ok = k.transform(o->body, toO3d(t), {0, 0, 1}, 0);
  } else if (auto o = op.as<op::ReplaceFace>()) {
    ok = k.replaceFace(o->body, o->face, o->targetBody, o->targetFace);
  } else if (auto o = op.as<op::Fill>()) {
    ok = k.fill(o->body, ints(o->edges)) > 0;
  } else if (auto o = op.as<op::Sweep>()) {
    auto p = replaySketches_.find(o->profile), q = replaySketches_.find(o->path);
    if (p == replaySketches_.end() || q == replaySketches_.end()) return std::string("The sweep's sketches are missing");
    ok = k.sweep(p->second.plane.bridged(), p->second.profiles(), q->second.plane.bridged(), q->second.path(),
                 bridged(o->mode), o->target.value_or(-1)) > 0;
  } else if (auto o = op.as<op::Loft>()) {
    std::vector<const Sketch*> sketches;
    for (auto& id : o->sections) {
      auto it = replaySketches_.find(id);
      if (it != replaySketches_.end()) sketches.push_back(&it->second);
    }
    if (sketches.size() != o->sections.size() || sketches.size() < 2) return std::string("The loft's sketches are missing");
    std::vector<o3d::Profile> profiles;
    std::vector<o3d::Plane> planes;
    for (auto* s : sketches) {
      auto ps = s->profiles();
      if (!ps.empty()) profiles.push_back(ps.front());
      planes.push_back(s->plane.bridged());
    }
    if (profiles.size() != sketches.size()) return std::string("Every loft sketch needs a closed profile");
    ok = k.loft(planes, profiles, bridged(o->mode), o->target.value_or(-1)) > 0;
  } else if (auto o = op.as<op::Scale>()) {
    ok = k.scale(o->body, o->factor, toO3d(o->center));
  } else if (auto o = op.as<op::Terrain>()) {
    const auto& t = o->settings;
    ok = k.terrain(toO3d(t.center), t.width, t.depth, t.nx, t.ny, t.topHeights()) > 0;
  } else if (auto o = op.as<op::ScaleAxes>()) {
    ok = k.scaleAxes(o->body, toO3d(o->factors), toO3d(o->center));
  } else if (auto o = op.as<op::Scatter>()) {
    const auto& p = o->settings;
    o3d::Kernel::ScatterOptions opts;
    opts.count = p.count;
    opts.seed = unsigned(p.seed);
    opts.minScale = p.minScale;
    opts.maxScale = p.maxScale;
    opts.spacing = p.spacing;
    opts.maxSlopeDeg = p.maxSlope;
    opts.sink = p.sink;
    opts.randomRotation = p.randomRotation;
    opts.alignToSurface = p.alignToSurface;
    opts.keepOriginal = p.keepOriginal;
    auto ids = k.scatter(o->source, o->target, o->avoid, opts);
    ok = !ids.empty();
    if (!ids.empty())
      for (int id : ids) scatterOrigins_[id] = {o->source, ids.front()};
  } else if (auto o = op.as<op::Emboss>()) {
    auto it = replaySketches_.find(o->sketch);
    if (it == replaySketches_.end()) return std::string("The emboss sketch is missing");
    ok = k.emboss(o->body, it->second.plane.bridged(), it->second.profiles(), o->height, o->deboss);
  } else if (auto o = op.as<op::LinearPattern>()) {
    ok = !k.linearPattern(o->body, toO3d(o->direction), o->count, o->spacing, o->fuse).empty();
  } else if (auto o = op.as<op::CircularPattern>()) {
    ok = !k.circularPattern(o->body, toO3d(o->origin), toO3d(o->axis), o->count, o->angle, o->fuse).empty();
  } else if (auto o = op.as<op::Split>()) {
    ok = !k.split(o->body, o->plane.transformed(o->offset, 0).bridged()).empty();
  } else if (auto o = op.as<op::DeleteFaces>()) {
    ok = k.deleteFaces(o->body, ints(o->faces));
  } else if (auto o = op.as<op::Draft>()) {
    ok = k.draft(o->body, ints(o->faces), toO3d(o->pull), o->angle);
  } else if (auto o = op.as<op::OffsetFaces>()) {
    ok = k.offsetFaces(o->body, ints(o->faces), o->distance);
  }
  if (ok) return std::nullopt;
  return k.lastError().empty() ? std::string("Operation failed") : k.lastError();
}

// MARK: Drafts

void ModelStore::begin(Operation op, std::optional<SketchSession> sketch, std::optional<Uuid> replacing) {
  // Face and edge indices change as soon as the preview alters topology.
  selection_.clear();
  Feature feature(op);
  if (replacing) {
    auto it = std::find_if(features_.begin(), features_.end(), [&](auto& f) { return f.id == *replacing; });
    if (it != features_.end()) {
      feature = *it;
      feature.op = std::move(op);
    }
  }
  draft_ = Draft{feature, replacing, std::move(sketch)};
  setSketch(std::nullopt);
  regenerate();
}

void ModelStore::edit(const Uuid& id) {
  auto it = std::find_if(features_.begin(), features_.end(), [&](auto& f) { return f.id == id; });
  if (it == features_.end()) return;
  setSketch(std::nullopt);
  draft_ = Draft{*it, id, std::nullopt};
  regenerate();
}

void ModelStore::updateDraft(Operation op) {
  if (!draft_) return;
  draft_->feature.op = std::move(op);
  if (draftRebuildPending_) return;
  draftRebuildPending_ = true;
  if (scheduleRebuild) {
    std::weak_ptr<char> alive = alive_;
    scheduleRebuild([this, alive] {
      if (alive.expired()) return;
      if (draftRebuildPending_) regenerate();
    });
    changed();
  } else {
    regenerate();
  }
}

void ModelStore::flushPendingRebuild() {
  if (draftRebuildPending_) regenerate();
}

void ModelStore::applyDraft() {
  if (draftRebuildPending_) regenerate();
  if (!draft_) return;
  auto error = errors_.find(draft_->feature.id);
  if (error != errors_.end()) {
    setToast(error->second);
    return;
  }
  std::vector<Feature> list = features_;
  auto it = draft_->replacing ? std::find_if(list.begin(), list.end(), [&](auto& f) { return f.id == *draft_->replacing; })
                              : list.end();
  if (it != list.end()) *it = draft_->feature;
  else list.push_back(draft_->feature);
  draft_.reset();
  selection_.clear();
  commit(std::move(list));
}

void ModelStore::cancelDraft() {
  std::optional<SketchSession> restored = draft_ ? draft_->sketch : std::nullopt;
  draft_.reset();
  setSketch(std::move(restored));
  regenerate();
}

std::map<int, Appearance> ModelStore::displayAppearance() const {
  auto looks = settings_.appearance;
  for (auto& [id, origin] : scatterOrigins_) {
    if (looks.count(id)) continue;
    auto a = looks.find(origin.first);
    if (a == looks.end()) a = looks.find(origin.second);
    if (a != looks.end()) {
      Appearance look = a->second;
      looks[id] = look;
    }
  }
  return looks;
}

void ModelStore::commitImmediately(Operation op) {
  // Trial-run as a draft so a failing operation never enters the history.
  Feature f(std::move(op));
  draft_ = Draft{f, std::nullopt, std::nullopt};
  regenerate();
  draft_.reset();
  auto error = errors_.find(f.id);
  if (error != errors_.end()) {
    setToast(error->second);
    regenerate();
    return;
  }
  selection_.clear();
  auto list = features_;
  list.push_back(f);
  commit(std::move(list));
}

// MARK: Selection

std::vector<int> ModelStore::selectedBodies() const {
  std::vector<int> out;
  for (auto& p : selection_)
    if (p.kind == Pick::Kind::Body) out.push_back(p.body);
  return out;
}

std::vector<std::pair<int, int>> ModelStore::selectedFaces() const {
  std::vector<std::pair<int, int>> out;
  for (auto& p : selection_)
    if (p.kind == Pick::Kind::Face) out.push_back({p.body, p.index});
  return out;
}

std::vector<std::pair<int, int>> ModelStore::selectedEdges() const {
  std::vector<std::pair<int, int>> out;
  for (auto& p : selection_)
    if (p.kind == Pick::Kind::Edge) out.push_back({p.body, p.index});
  return out;
}

void ModelStore::select(std::optional<Pick> pick, bool additive) {
  if (!pick) {
    if (!additive) setSelection({});
    return;
  }
  auto it = std::find(selection_.begin(), selection_.end(), *pick);
  if (it != selection_.end()) {
    auto s = selection_;
    s.erase(s.begin() + (it - selection_.begin()));
    setSelection(std::move(s));
    return;
  }
  // Edges and faces of the same body accumulate naturally, which is what
  // fillet and shell need; anything else replaces the selection.
  bool sameKind = std::all_of(selection_.begin(), selection_.end(), [&](const Pick& e) {
    if (e.kind == Pick::Kind::Edge && pick->kind == Pick::Kind::Edge) return e.body == pick->body;
    if (e.kind == Pick::Kind::Face && pick->kind == Pick::Kind::Face) return e.body == pick->body;
    if (e.kind == Pick::Kind::Body && pick->kind == Pick::Kind::Body) return additive;
    return false;
  });
  std::vector<Pick> s;
  if (additive || sameKind) s = selection_;
  s.push_back(*pick);
  setSelection(std::move(s));
}

std::optional<o3d::FaceInfo> ModelStore::faceInfo(int body, int face) const {
  o3d::FaceInfo info;
  if (!kernel_->faceInfo(body, face, info)) return std::nullopt;
  return info;
}

std::optional<o3d::EdgeInfo> ModelStore::edgeInfo(int body, int edge) const {
  o3d::EdgeInfo info;
  if (!kernel_->edgeInfo(body, edge, info)) return std::nullopt;
  return info;
}

std::optional<o3d::Properties> ModelStore::properties(int body) {
  uint64_t key = kernel_->shapeKey(body);
  auto mk = meshKeys_.find(body);
  bool current = key != 0 && mk != meshKeys_.end() && mk->second == key;
  if (current) {
    auto c = propertiesCache_.find(body);
    if (c != propertiesCache_.end() && c->second.first == key) return c->second.second;
  }
  o3d::Properties p;
  if (!kernel_->properties(body, p)) return std::nullopt;
  if (current) propertiesCache_[body] = {key, p};
  return p;
}

// MARK: Commands

void ModelStore::addPrimitive(PrimitiveKind kind) {
  selection_.clear();
  begin(op::Primitive{kind, defaultDims(kind), Vec3d()});
}

void ModelStore::startSketch(const SketchPlane& plane, std::optional<int> target) {
  draft_.reset();
  selection_.clear();
  setSketch(SketchSession(Sketch(plane), target));
  regenerate();
}

void ModelStore::startSketchOnSelectedFace() {
  auto faces = selectedFaces();
  std::optional<o3d::FaceInfo> info;
  if (!faces.empty()) info = faceInfo(faces[0].first, faces[0].second);
  if (!info || !info->planar) {
    setToast(std::string("Select a flat face to sketch on"));
    return;
  }
  startSketch(SketchPlane{fromO3d(info->centroid), fromO3d(info->normal), fromO3d(info->xDir)}, faces[0].first);
}

void ModelStore::editSketch(const Uuid& id) {
  auto it = std::find_if(features_.begin(), features_.end(), [&](auto& f) { return f.id == id; });
  if (it == features_.end() || !it->op.sketch()) return;
  Sketch sk = *it->op.sketch();
  draft_.reset();
  selection_.clear();
  SketchSession session(sk, it->op.sketchTarget(), id, SketchTool::select);
  if (auto status = sk.solve()) {
    session.sketch = sk;
    session.status = *status;
  }
  setSketch(std::move(session));
  regenerate();
}

void ModelStore::finishSketch(bool revolve) {
  if (!sketch_) return;
  SketchSession session = *sketch_;
  session.preview.clear();
  session.chainEnd.reset();
  session.sketch.remove({});
  if (session.sketch.profileCount() == 0) {
    setToast(std::string("Draw a closed shape first"));
    return;
  }
  Sketch sk = session.sketch;
  if (session.replacing) {
    Uuid r = *session.replacing;
    auto it = std::find_if(features_.begin(), features_.end(), [&](auto& f) { return f.id == r; });
    if (it != features_.end()) {
      Operation existing = it->op;
      if (existing.is<op::SketchItem>()) {
        setSketch(std::nullopt);
        std::vector<Feature> list;
        for (auto& f : features_) list.push_back(f.id == r ? Feature(r, existing.setting(sk), f.suppressed) : f);
        commit(std::move(list));
        return;
      }
      begin(existing.setting(sk), session, r);
      return;
    }
  }
  CombineMode mode = session.target ? CombineMode::join : CombineMode::newBody;
  if (revolve) begin(op::Revolve{sk, 360, true, mode, session.target}, session);
  else begin(op::Extrude{sk, 20, mode, session.target}, session);
}

static bool isSketchOp(const Operation& o) { return o.is<op::SketchItem>(); }

void ModelStore::finishSketchAsItem() {
  if (!sketch_) return;
  SketchSession session = *sketch_;
  session.sketch.remove({});
  if (session.sketch.isEmpty()) return cancelSketch();
  if (session.replacing) return finishSketch(false);
  int n = count(isSketchOp);
  setSketch(std::nullopt);
  auto list = features_;
  list.push_back(Feature(op::SketchItem{session.sketch, "Sketch " + std::to_string(n + 1)}));
  commit(std::move(list));
}

void ModelStore::cancelSketch() {
  bool wasEditing = sketch_ && sketch_->replacing;
  setSketch(std::nullopt);
  if (wasEditing) regenerate();
}

void ModelStore::setSketchTool(SketchTool tool) {
  if (!sketch_) return;
  SketchSession s = *sketch_;
  endChain(s);
  s.tool = tool;
  s.preview.clear();
  s.cursor.reset();
  setSketch(std::move(s));
}

void ModelStore::sketchEscape() {
  if (!sketch_) return;
  SketchSession s = *sketch_;
  if (s.chainEnd || !s.pending.empty() || !s.splineDraft.empty()) {
    endChain(s);
    setSketch(std::move(s));
  } else if (!s.selection.empty()) {
    s.selection.clear();
    setSketch(std::move(s));
  } else {
    cancelSketch();
  }
}

void ModelStore::endChain(SketchSession& s) {
  s.pending.clear();
  if (s.splineDraft.size() >= 2) {
    SketchSpline sp;
    sp.points = s.splineDraft;
    sp.construction = s.constructionMode;
    s.sketch.splines.push_back(sp);
    s.splineDraft.clear();
    s.resolve();
  }
  s.splineDraft.clear();
  if (!s.chainEnd) return;
  s.chainEnd.reset();
  // A chain that never got a second point leaves a stray point behind.
  size_t n = s.sketch.points.size();
  s.sketch.remove({});
  if (s.sketch.points.size() != n) {
    s.selection.clear();
    s.hover.reset();
    s.resolve();
  }
}

void ModelStore::beginSketchDrag() {
  if (!sketch_ || sketch_->dragging) return;
  sketchUndo_.push_back(sketch_->sketch);
  sketchRedo_.clear();
  updateSketch([](SketchSession& s) { s.dragging = true; });
}

void ModelStore::endSketchDrag() {
  updateSketch([](SketchSession& s) { s.dragging = false; });
}

void ModelStore::undoSketch() {
  if (!sketch_ || sketchUndo_.empty()) return;
  SketchSession s = *sketch_;
  Sketch previous = sketchUndo_.back();
  sketchUndo_.pop_back();
  sketchRedo_.push_back(s.sketch);
  restore(s, previous);
}

void ModelStore::redoSketch() {
  if (!sketch_ || sketchRedo_.empty()) return;
  SketchSession s = *sketch_;
  Sketch next = sketchRedo_.back();
  sketchRedo_.pop_back();
  sketchUndo_.push_back(s.sketch);
  restore(s, next);
}

void ModelStore::restore(SketchSession& s, const Sketch& geometry) {
  s.sketch = geometry;
  s.selection.clear();
  s.hover.reset();
  s.chainEnd.reset();
  s.pending.clear();
  s.resolve();
  restoringSketch_ = true;
  setSketch(s);
  restoringSketch_ = false;
}

void ModelStore::setPolygonSides(int n) { updateSketch([&](SketchSession& s) { s.polygonSides = n; }); }
void ModelStore::setPolygonCircumscribed(bool on) { updateSketch([&](SketchSession& s) { s.polygonCircumscribed = on; }); }
void ModelStore::setConstructionMode(bool on) { updateSketch([&](SketchSession& s) { s.constructionMode = on; }); }
void ModelStore::setOffsetDistance(double d) { updateSketch([&](SketchSession& s) { s.offsetDistance = d; }); }
void ModelStore::setFilletRadius(double r) { updateSketch([&](SketchSession& s) { s.filletRadius = r; }); }
void ModelStore::setSketchSelection(std::vector<SketchRef> refs) {
  updateSketch([&](SketchSession& s) { s.selection = refs; });
}

void ModelStore::toggleSketchConstraintSelection(int index) {
  if (index < 0) return;
  updateSketch([&](SketchSession& s) {
    auto ref = SketchRef::constraint(index);
    auto it = std::find(s.selection.begin(), s.selection.end(), ref);
    if (it != s.selection.end()) s.selection.erase(it);
    else s.selection = {ref};
  });
}

// MARK: Sketch constraints

void ModelStore::constrainSelection(SketchConstraint::Kind kind) {
  if (!sketch_) return;
  SketchSession s = *sketch_;
  if (auto error = s.constrain(kind)) setToast(*error);
  else setSketch(std::move(s));
}

void ModelStore::setDimension(int index, double value) {
  if (!sketch_) return;
  SketchSession s = *sketch_;
  if (auto error = s.setDimension(index, value)) setToast(*error);
  else setSketch(std::move(s));
}

void ModelStore::deleteSketchSelection() {
  if (!sketch_ || sketch_->selection.empty()) return;
  SketchSession s = *sketch_;
  s.sketch.remove(std::set<SketchRef>(s.selection.begin(), s.selection.end()));
  s.selection.clear();
  s.hover.reset();
  s.chainEnd.reset();
  s.resolve();
  setSketch(std::move(s));
}

void ModelStore::constrainHorizontalOrVertical() {
  if (!sketch_) return;
  SketchSession s = *sketch_;
  for (int l : s.selectedLines()) {
    Vec2d d = s.sketch.direction(l);
    s.selection = {SketchRef::line(l)};
    auto kind = std::abs(d.x) >= std::abs(d.y) ? SketchConstraint::Kind::Horizontal : SketchConstraint::Kind::Vertical;
    if (auto error = s.constrain(kind)) setToast(*error);
  }
  s.selection.clear();
  setSketch(std::move(s));
}

void ModelStore::offsetSketchSelection() {
  if (!sketch_) return;
  SketchSession s = *sketch_;
  std::vector<SketchRef> refs;
  for (auto& r : s.selection)
    if (r.kind != SketchRef::Kind::Constraint) refs.push_back(r);
  auto created = s.sketch.offset(refs, s.offsetDistance);
  if (created.empty()) return setToast(std::string("Select lines, arcs or circles to offset"));
  s.selection = created;
  s.resolve();
  setSketch(std::move(s));
}

void ModelStore::filletSketchSelection() {
  if (!sketch_) return;
  SketchSession s = *sketch_;
  // A selected corner point, or the point two selected lines share.
  std::optional<int> corner;
  auto pts = s.selectedPoints();
  if (!pts.empty()) corner = pts.front();
  auto lns = s.selectedLines();
  if (!corner && lns.size() == 2) {
    auto a = s.sketch.lines[size_t(lns[0])], b = s.sketch.lines[size_t(lns[1])];
    for (int p : {a.a, a.b})
      if (p == b.a || p == b.b) { corner = p; break; }
  }
  if (!corner) return setToast(std::string("Select a corner, or two lines that meet"));
  if (auto error = s.sketch.fillet(*corner, s.filletRadius)) return setToast(*error);
  s.selection.clear();
  s.resolve();
  setSketch(std::move(s));
}

void ModelStore::disconnectSketchSelection() {
  if (!sketch_) return;
  SketchSession s = *sketch_;
  auto pts = s.selectedPoints();
  if (pts.empty()) return;
  s.sketch.disconnect(pts.front());
  s.selection.clear();
  s.resolve();
  setSketch(std::move(s));
}

void ModelStore::toggleConstruction() {
  if (!sketch_) return;
  SketchSession s = *sketch_;
  for (auto& r : s.selection) {
    switch (r.kind) {
      case SketchRef::Kind::Line: s.sketch.lines[size_t(r.index)].construction ^= true; break;
      case SketchRef::Kind::Circle: s.sketch.circles[size_t(r.index)].construction ^= true; break;
      case SketchRef::Kind::Spline: s.sketch.splines[size_t(r.index)].construction ^= true; break;
      case SketchRef::Kind::Ellipse: s.sketch.ellipses[size_t(r.index)].construction ^= true; break;
      default: break;
    }
  }
  setSketch(std::move(s));
}

void ModelStore::pushPullSelection() {
  auto faces = selectedFaces();
  if (faces.empty()) return;
  auto info = faceInfo(faces[0].first, faces[0].second);
  if (!info || !info->planar) return setToast(std::string("Push/pull needs a flat face"));
  begin(op::PushPull{faces[0].first, faces[0].second, 10});
}

void ModelStore::filletSelection(bool chamfer) {
  auto edges = selectedEdges();
  if (edges.empty()) return;
  int body = edges[0].first;
  std::vector<int> ids;
  for (auto& e : edges)
    if (e.first == body) ids.push_back(e.second);
  if (chamfer) begin(op::Chamfer{body, ids, 2});
  else begin(op::Fillet{body, ids, 3});
}

void ModelStore::shellSelection() {
  auto faces = selectedFaces();
  if (!faces.empty()) {
    int body = faces[0].first;
    std::vector<int> ids;
    for (auto& f : faces)
      if (f.first == body) ids.push_back(f.second);
    begin(op::Shell{body, ids, 2});
  } else if (!selectedBodies().empty()) {
    begin(op::Shell{selectedBodies().front(), {}, 2});
  }
}

void ModelStore::moveSelection() {
  if (selection_.empty()) return;
  begin(op::Move{selection_.front().body, Vec3d(), Vec3d(0, 0, 1), 0});
}

void ModelStore::duplicateSelection() {
  if (selection_.empty()) return;
  commitImmediately(op::Duplicate{selection_.front().body});
}

void ModelStore::deleteSelection() {
  std::set<int> bodies;
  for (auto& p : selection_) bodies.insert(p.body);
  if (bodies.empty()) return;
  auto list = features_;
  for (int b : bodies) list.push_back(Feature(op::Delete{b}));
  selection_.clear();
  commit(std::move(list));
}

static std::vector<int> uniqueBodies(const std::vector<Pick>& selection) {
  std::vector<int> out;
  for (auto& p : selection)
    if (std::find(out.begin(), out.end(), p.body) == out.end()) out.push_back(p.body);
  return out;
}

void ModelStore::boolean(BooleanKind kind) {
  auto bodies = uniqueBodies(selection_);
  if (bodies.size() != 2) return setToast(std::string("Select two bodies"));
  commitImmediately(op::Boolean{kind, bodies[0], bodies[1]});
}

// MARK: Exchange

void ModelStore::importFile(const std::string& path) {
  o3d::Kernel scratch;
  auto ids = scratch.importFile(path);
  if (ids.empty()) {
    setToast(scratch.lastError().empty() ? std::string("Import failed") : scratch.lastError());
    return;
  }
  std::string name = fs::path(path).stem().string();
  auto list = features_;
  for (int id : ids) {
    std::string brep = scratch.toBrep(id);
    if (!brep.empty()) list.push_back(Feature(op::Imported{name, brep}));
  }
  commit(std::move(list));
}

static std::optional<std::vector<uint8_t>> readFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::optional<std::vector<uint8_t>> ModelStore::exportExchange(ExchangeFormat format) {
  std::error_code ec;
  fs::path dir = fs::temp_directory_path(ec);
  fs::path path = dir / ("sculpt3d-export-" + Uuid::random().string() + "." + rawValue(format));
  std::vector<int> bodies;
  if (selection_.empty()) {
    bodies = bodyIds();
  } else {
    std::set<int> picked;
    for (auto& p : selection_) picked.insert(p.body);
    bodies.assign(picked.begin(), picked.end());
  }
  if (bodies.empty()) {
    setToast(std::string("Nothing to export"));
    return std::nullopt;
  }
  if (!kernel_->exportFile(path.string(), bodies)) {
    setToast(kernel_->lastError().empty() ? std::string("Export failed") : kernel_->lastError());
    fs::remove(path, ec);
    return std::nullopt;
  }
  auto data = readFile(path);
  fs::remove(path, ec);
  return data;
}

int ModelStore::count(bool (*kind)(const Operation&)) const {
  return int(std::count_if(features_.begin(), features_.end(), [&](auto& f) { return kind(f.op); }));
}

Vec3d ModelStore::centerOf(int body) const {
  auto m = meshes_.find(body);
  return m == meshes_.end() ? Vec3d() : m->second->center();
}

}  // namespace sculpt
