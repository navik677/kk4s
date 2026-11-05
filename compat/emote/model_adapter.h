// kk4s E-mote runtime: adapters from a PSB Value tree to CharacterModel.
//
// One adapter per content schema. An adapter claims a document only from
// markers inside the data (spec / format / version keys), never from a game
// name or a file name. A document no adapter claims is UnsupportedSchema.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "emote/easing.h"
#include "emote/emote_model.h"
#include "emote/emote_status.h"
#include "emote/psb_value.h"

namespace kk4s {
namespace emote {

class Document;

class ModelAdapter {
public:
    virtual ~ModelAdapter() {}
    virtual const char* id() const = 0;
    // Ok when this adapter understands the document; the message says why not.
    virtual Status Probe(const Value& root) const = 0;
    virtual Status Build(const Value& root, CharacterModel& out) const = 0;
    // Stored easing code -> curve, as far as known for this schema.
    virtual EasingMap Easing() const = 0;
};

class AdapterRegistry {
public:
    Status Register(std::unique_ptr<ModelAdapter> a);
    const std::vector<std::unique_ptr<ModelAdapter>>& adapters() const { return adapters_; }
    // Picks the single adapter whose Probe succeeds and builds the model.
    Status BuildModel(const Value& root, CharacterModel& out) const;
    const ModelAdapter* Find(const std::string& id) const;
    static const AdapterRegistry& Builtin();

private:
    std::vector<std::unique_ptr<ModelAdapter>> adapters_;
};

// E-mote content version 3.03, metadata.format "emote" / version 1, spec krkr or win
// (compat/emote/adapters/emote_v3.cpp).
std::unique_ptr<ModelAdapter> MakeEmoteV3Adapter();

// Parses a layer source reference in either stored form:
//   path form  "src/<source>/<icon>", "motion/<object>/<motion>", "blank/<w:h:ox:oy>",
//              "layout", "shape/<name>"
//   pair form  src "<source or object>" + icon "<icon or motion>", src "blank" + icon "<w:h:ox:oy>"
// `model` resolves pair-form names against its sources and objects.
SourceRef ParseSourceRef(const std::string& src, const std::string* icon, const CharacterModel& model);

}  // namespace emote
}  // namespace kk4s
