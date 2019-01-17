
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2017 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    MoveObjects.cpp
// Description: MoveObjects class - handles object moving operations in the map
//              editor
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110 - 1301, USA.
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//
// Includes
//
// -----------------------------------------------------------------------------
#include "Main.h"
#include "MoveObjects.h"
#include "MapEditor/MapEditContext.h"
#include "MapEditor/UndoSteps.h"


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
CVAR(Bool, map_merge_undo_step, true, CVar::Flag::Save)
CVAR(Bool, selection_clear_move, true, CVar::Flag::Save)


// -----------------------------------------------------------------------------
//
// MoveObjects Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// MoveObjects class constructor
// -----------------------------------------------------------------------------
MoveObjects::MoveObjects(MapEditContext& context) : context_{ context } {}

// -----------------------------------------------------------------------------
// Begins a move operation, starting from [mouse_pos]
// -----------------------------------------------------------------------------
bool MoveObjects::begin(Vec2f mouse_pos)
{
	using MapEditor::Mode;

	// Check if we have any selection or hilight
	if (!context_.selection().hasHilightOrSelection())
		return false;

	// Begin move operation
	origin_ = mouse_pos;
	items_  = context_.selection().selectionOrHilight();

	// Get list of vertices being moved (if any)
	vector<MapVertex*> move_verts;
	if (context_.editMode() != Mode::Things)
	{
		// Vertices mode
		if (context_.editMode() == Mode::Vertices)
		{
			for (auto& item : items_)
				move_verts.push_back(context_.map().vertex(item.index));
		}

		// Lines mode
		else if (context_.editMode() == Mode::Lines)
		{
			for (auto& item : items_)
			{
				// Duplicate vertices shouldn't matter here
				move_verts.push_back(context_.map().line(item.index)->v1());
				move_verts.push_back(context_.map().line(item.index)->v2());
			}
		}

		// Sectors mode
		else if (context_.editMode() == Mode::Sectors)
		{
			for (auto& item : items_)
				context_.map().sector(item.index)->putVertices(move_verts);
		}
	}

	// Filter out map objects being moved
	if (context_.editMode() == Mode::Things)
	{
		// Filter moving things
		for (auto& item : items_)
			context_.map().thing(item.index)->filter(true);
	}
	else
	{
		// Filter moving lines
		for (auto vertex : move_verts)
		{
			for (unsigned l = 0; l < vertex->nConnectedLines(); l++)
				vertex->connectedLine(l)->filter(true);
		}
	}

	return true;
}

// -----------------------------------------------------------------------------
// Updates the current move operation (moving from start to [mouse_pos])
// -----------------------------------------------------------------------------
void MoveObjects::update(Vec2f mouse_pos)
{
	using MapEditor::Mode;

	// Special case: single vertex or thing
	if (items_.size() == 1 && (context_.editMode() == Mode::Vertices || context_.editMode() == Mode::Things))
	{
		// Get new position
		double nx = context_.snapToGrid(mouse_pos.x, false);
		double ny = context_.snapToGrid(mouse_pos.y, false);

		// Update move vector
		if (context_.editMode() == Mode::Vertices)
			offset_.set(
				nx - context_.map().vertex(items_[0].index)->xPos(),
				ny - context_.map().vertex(items_[0].index)->yPos());
		else if (context_.editMode() == Mode::Things)
			offset_.set(
				nx - context_.map().thing(items_[0].index)->xPos(), ny - context_.map().thing(items_[0].index)->yPos());

		return;
	}

	// Update move vector
	offset_.set(
		context_.snapToGrid(mouse_pos.x - origin_.x, false), context_.snapToGrid(mouse_pos.y - origin_.y, false));
}

// -----------------------------------------------------------------------------
// Ends a move operation and applies change if [accept] is true
// -----------------------------------------------------------------------------
void MoveObjects::end(bool accept)
{
	using MapEditor::Mode;

	// Un-filter objects
	for (unsigned a = 0; a < context_.map().nLines(); a++)
		context_.map().line(a)->filter(false);
	for (unsigned a = 0; a < context_.map().nThings(); a++)
		context_.map().thing(a)->filter(false);

	// Move depending on edit mode
	if (context_.editMode() == Mode::Things && accept)
	{
		// Move things
		context_.beginUndoRecord("Move Things", true, false, false);
		for (auto& item : items_)
		{
			auto thing = context_.map().thing(item.index);
			context_.undoManager()->recordUndoStep(std::make_unique<MapEditor::PropertyChangeUS>(thing));
			context_.map().moveThing(item.index, thing->xPos() + offset_.x, thing->yPos() + offset_.y);
		}
		context_.endUndoRecord(true);
	}
	else if (accept)
	{
		// Any other edit mode we're technically moving vertices
		context_.beginUndoRecord(S_FMT("Move %s", context_.modeString()));

		// Get list of vertices being moved
		vector<uint8_t> move_verts(context_.map().nVertices());
		memset(&move_verts[0], 0, context_.map().nVertices());

		if (context_.editMode() == Mode::Vertices)
		{
			for (auto& item : items_)
				move_verts[item.index] = 1;
		}
		else if (context_.editMode() == Mode::Lines)
		{
			for (auto& item : items_)
			{
				auto line = context_.map().line(item.index);
				if (line->v1())
					move_verts[line->v1()->index()] = 1;
				if (line->v2())
					move_verts[line->v2()->index()] = 1;
			}
		}
		else if (context_.editMode() == Mode::Sectors)
		{
			vector<MapVertex*> sv;
			for (auto& item : items_)
				context_.map().sector(item.index)->putVertices(sv);

			for (auto vertex : sv)
				move_verts[vertex->index()] = 1;
		}

		// Move vertices
		vector<MapVertex*> moved_verts;
		for (unsigned a = 0; a < context_.map().nVertices(); a++)
		{
			if (!move_verts[a])
				continue;

			context_.map().moveVertex(
				a, context_.map().vertex(a)->xPos() + offset_.x, context_.map().vertex(a)->yPos() + offset_.y);

			moved_verts.push_back(context_.map().vertex(a));
		}

		// Begin extra 'Merge' undo step if wanted
		if (map_merge_undo_step)
		{
			context_.endUndoRecord(true);
			context_.beginUndoRecord("Merge");
		}

		// Do merge
		bool merge = context_.map().mergeArch(moved_verts);

		context_.endUndoRecord(merge || !map_merge_undo_step);
	}

	// Clear selection
	if (accept && selection_clear_move)
		context_.selection().clear();

	// Clear moving items
	items_.clear();

	// Update map item indices
	context_.map().refreshIndices();
}
