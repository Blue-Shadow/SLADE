#pragma once

#include "General/ListenerAnnouncer.h"

class UndoStep
{
public:
	UndoStep()          = default;
	virtual ~UndoStep() = default;

	virtual bool doUndo() { return true; }
	virtual bool doRedo() { return true; }
	virtual bool writeFile(MemChunk& mc) { return true; }
	virtual bool readFile(MemChunk& mc) { return true; }
	virtual bool isOk() { return true; }

	typedef std::unique_ptr<UndoStep> UPtr;
};

class UndoLevel
{
public:
	typedef std::unique_ptr<UndoLevel> UPtr;

	UndoLevel(const string& name);
	~UndoLevel() = default;

	string name() const { return name_; }
	bool   doUndo();
	bool   doRedo();
	void   addStep(UndoStep::UPtr step) { undo_steps_.push_back(std::move(step)); }
	string timeStamp(bool date, bool time) const;

	bool writeFile(const string& filename) const;
	bool readFile(const string& filename) const;
	void createMerged(vector<UPtr>& levels);

private:
	string                 name_;
	vector<UndoStep::UPtr> undo_steps_;
	wxDateTime             timestamp_;
};

class SLADEMap;
class UndoManager : public Announcer
{
public:
	UndoManager(SLADEMap* map = nullptr) : map_{ map } {}
	~UndoManager() = default;

	SLADEMap*  map() const { return map_; }
	void       putAllLevels(vector<string>& list);
	int        currentIndex() const { return current_level_index_; }
	unsigned   nUndoLevels() const { return undo_levels_.size(); }
	UndoLevel* undoLevel(unsigned index) const { return undo_levels_[index].get(); }

	void   beginRecord(const string& name);
	void   endRecord(bool success);
	bool   currentlyRecording() const;
	bool   recordUndoStep(UndoStep::UPtr step) const;
	string undo();
	string redo();
	void   setResetPoint() { reset_point_ = current_level_index_; }
	void   clearToResetPoint();

	void clear();
	bool createMergedLevel(UndoManager* manager, const string& name);

	typedef std::unique_ptr<UndoManager> UPtr;

private:
	vector<UndoLevel::UPtr> undo_levels_;
	UndoLevel::UPtr         current_level_;
	int                     current_level_index_ = -1;
	int                     reset_point_         = -1;
	bool                    undo_running_        = false;
	SLADEMap*               map_                 = nullptr;
};

namespace UndoRedo
{
bool         currentlyRecording();
UndoManager* currentManager();
SLADEMap*    currentMap();
} // namespace UndoRedo
