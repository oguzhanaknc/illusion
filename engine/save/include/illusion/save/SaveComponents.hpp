#pragma once

/// @file
/// The Component that marks what a saved game contains (docs/adr/0058-saved-games.md).

namespace illusion
{

/// Tag of an Entity that saved games contain, with all its descendants (reflected as "illusion.Saved").
struct Saved
{
};

/// Reflects the Components of the save library; call once with the other reflected Components.
void registerSaveComponents();

} // namespace illusion
