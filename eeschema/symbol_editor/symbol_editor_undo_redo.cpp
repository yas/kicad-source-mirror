/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2007 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2014-2020 KiCad Developers, see CHANGELOG.TXT for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <symbol_edit_frame.h>
#include <symbol_library_manager.h>
#include <widgets/lib_tree.h>
#include <symbol_tree_pane.h>
#include <tool/tool_manager.h>
#include <tools/ee_actions.h>
#include <tools/ee_selection_tool.h>

void SYMBOL_EDIT_FRAME::SaveCopyInUndoList( EDA_ITEM* aItem, UNDO_REDO aUndoType, bool aAppend )
{
    wxASSERT_MSG( !aAppend, "Append not needed/supported for symbol editor" );

    if( !aItem )
        return;

    LIB_PART*          copyItem;
    PICKED_ITEMS_LIST* lastcmd = new PICKED_ITEMS_LIST();

    copyItem = new LIB_PART( * (LIB_PART*) aItem );

    // Clear current flags (which can be temporary set by a current edit command).
    copyItem->ClearTempFlags();
    copyItem->ClearEditFlags();
    copyItem->SetFlags( UR_TRANSIENT );

    ITEM_PICKER wrapper( GetScreen(), copyItem, aUndoType );
    lastcmd->PushItem( wrapper );
    PushCommandToUndoList( lastcmd );

    // Clear redo list, because after new save there is no redo to do.
    ClearUndoORRedoList( REDO_LIST );
}


void SYMBOL_EDIT_FRAME::GetSymbolFromRedoList()
{
    if( GetRedoCommandCount() <= 0 )
        return;

    m_toolManager->RunAction( EE_ACTIONS::clearSelection, true );

    // Load the last redo entry
    PICKED_ITEMS_LIST* redoCommand = PopCommandFromRedoList();
    ITEM_PICKER redoWrapper = redoCommand->PopItem();
    delete redoCommand;
    LIB_PART* part = (LIB_PART*) redoWrapper.GetItem();
    wxCHECK( part, /* void */ );
    part->ClearFlags( UR_TRANSIENT );
    UNDO_REDO undoRedoType = redoWrapper.GetStatus();

    // Store the current part in the undo buffer
    PICKED_ITEMS_LIST* undoCommand = new PICKED_ITEMS_LIST();
    LIB_PART* oldPart = m_my_part;
    oldPart->SetFlags( UR_TRANSIENT );
    ITEM_PICKER undoWrapper( GetScreen(), oldPart, undoRedoType );
    undoCommand->PushItem( undoWrapper );
    PushCommandToUndoList( undoCommand );

    // Do not delete the previous part by calling SetCurPart( part )
    // which calls delete <previous part>.
    // <previous part> is now put in undo list and is owned by this list
    // Just set the current part to the part which come from the redo list
    m_my_part = part;

    if( undoRedoType == UNDO_REDO::LIB_RENAME )
    {
        wxString lib = GetCurLib();
        m_libMgr->UpdatePartAfterRename( part, oldPart->GetName(), lib );

        // Reselect the renamed part
        m_treePane->GetLibTree()->SelectLibId( LIB_ID( lib, part->GetName() ) );
    }

    RebuildSymbolUnitsList();
    SetShowDeMorgan( part->HasConversion() );
    updateTitle();

    RebuildView();
    OnModify();
}


void SYMBOL_EDIT_FRAME::GetSymbolFromUndoList()
{
    if( GetUndoCommandCount() <= 0 )
        return;

    m_toolManager->RunAction( EE_ACTIONS::clearSelection, true );

    // Load the last undo entry
    PICKED_ITEMS_LIST* undoCommand = PopCommandFromUndoList();
    ITEM_PICKER undoWrapper = undoCommand->PopItem();
    delete undoCommand;
    LIB_PART* part = (LIB_PART*) undoWrapper.GetItem();
    wxCHECK( part, /* void */ );
    part->ClearFlags( UR_TRANSIENT );
    UNDO_REDO undoRedoType = undoWrapper.GetStatus();

    // Store the current part in the redo buffer
    PICKED_ITEMS_LIST* redoCommand = new PICKED_ITEMS_LIST();
    LIB_PART* oldPart = m_my_part;
    oldPart->SetFlags( UR_TRANSIENT );
    ITEM_PICKER redoWrapper( GetScreen(), oldPart, undoRedoType );
    redoCommand->PushItem( redoWrapper );
    PushCommandToRedoList( redoCommand );

    // Do not delete the previous part by calling SetCurPart( part ),
    // which calls delete <previous part>.
    // <previous part> is now put in redo list and is owned by this list.
    // Just set the current part to the part which come from the undo list
    m_my_part = part;

    if( undoRedoType == UNDO_REDO::LIB_RENAME )
    {
        wxString lib = GetCurLib();
        m_libMgr->UpdatePartAfterRename( part, oldPart->GetName(), lib );

        // Reselect the renamed part
        m_treePane->GetLibTree()->SelectLibId( LIB_ID( lib, part->GetName() ) );
    }

    RebuildSymbolUnitsList();
    SetShowDeMorgan( part->HasConversion() );
    updateTitle();

    RebuildView();
    OnModify();
}


void SYMBOL_EDIT_FRAME::RollbackSymbolFromUndo()
{
    m_toolManager->RunAction( EE_ACTIONS::clearSelection, true );

    // Load the last undo entry
    PICKED_ITEMS_LIST* undoCommand = PopCommandFromUndoList();

    // Check if we were already at the top of the stack
    if( !undoCommand )
        return;

    ITEM_PICKER undoWrapper = undoCommand->PopItem();
    delete undoCommand;
    LIB_PART* part = (LIB_PART*) undoWrapper.GetItem();
    part->ClearFlags( UR_TRANSIENT );
    SetCurPart( part, false );

    EE_SELECTION_TOOL* selTool = m_toolManager->GetTool<EE_SELECTION_TOOL>();
    selTool->RebuildSelection();

    RebuildSymbolUnitsList();
    SetShowDeMorgan( part->HasConversion() );

    RebuildView();
}
