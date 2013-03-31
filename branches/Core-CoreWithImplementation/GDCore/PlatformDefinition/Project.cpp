/** \file
 *  Game Develop
 *  2008-2013 Florian Rival (Florian.Rival@gmail.com)
 */

#include <map>
#include <vector>
#include <string>
#include <wx/propgrid/propgrid.h>
#include <wx/settings.h>
#include <wx/log.h>
#include "GDCore/IDE/Dialogs/ProjectExtensionsDialog.h"
#include "GDCore/IDE/Dialogs/ChooseVariableDialog.h"
#include "GDCore/IDE/ArbitraryResourceWorker.h"
#include "GDCore/PlatformDefinition/Platform.h"
#include "GDCore/PlatformDefinition/PlatformExtension.h"
#include "GDCore/PlatformDefinition/Layout.h"
#include "GDCore/PlatformDefinition/ExternalEvents.h"
#include "GDCore/PlatformDefinition/Object.h"
#include "GDCore/PlatformDefinition/ResourcesManager.h"
#include "GDCore/PlatformDefinition/ChangesNotifier.h"
#include "GDCore/Events/ExpressionMetadata.h"
#include "GDCore/PlatformDefinition/InstructionsMetadataHolder.h"
#include "GDCore/CommonTools.h"
#include "GDCore/TinyXml/tinyxml.h"
#include "Project.h"

namespace gd
{

std::vector < std::string > Project::noPlatformExtensionsUsed;
ChangesNotifier Project::defaultEmptyChangesNotifier;

Project::Project()
{
    //ctor
}

Project::~Project()
{
    //dtor
}

void Project::ExposeResources(gd::ArbitraryResourceWorker & worker)
{
    //Add project resources
    std::vector<std::string> resources = GetResourcesManager().GetAllResourcesList();
    for ( unsigned int i = 0;i < resources.size() ;i++ )
    {
        if ( GetResourcesManager().GetResource(resources[i]).UseFile() )
            worker.ExposeResource(GetResourcesManager().GetResource(resources[i]).GetFile());
    }
    wxSafeYield();

    //Add layouts resources
    for ( unsigned int s = 0;s < GetLayoutCount();s++ )
    {
        for (unsigned int j = 0;j<GetLayout(s).GetObjectsCount();++j) //Add objects resources
        	GetLayout(s).GetObject(j).ExposeResources(worker);

        LaunchResourceWorkerOnEvents(*this, GetLayout(s).GetEvents(), worker);
    }
    //Add external events resources
    for ( unsigned int s = 0;s < GetExternalEventsCount();s++ )
    {
        LaunchResourceWorkerOnEvents(*this, GetExternalEvents(s).GetEvents(), worker);
    }
    wxSafeYield();

    //Add global objects resources
    for (unsigned int j = 0;j<GetObjectsCount();++j)
        GetObject(j).ExposeResources(worker);
    wxSafeYield();
}

void Project::PopulatePropertyGrid(wxPropertyGrid * grid)
{
    grid->Append( new wxPropertyCategory(_("Properties")) );
    grid->Append( new wxStringProperty(_("Name of the project"), wxPG_LABEL, GetName()) );
    grid->Append( new wxStringProperty(_("Author"), wxPG_LABEL, GetAuthor()) );
    grid->Append( new wxStringProperty(_("Globals variables"), wxPG_LABEL, _("Click to edit...")) );
    grid->Append( new wxStringProperty(_("Extensions"), wxPG_LABEL, _("Click to edit...")) );
    grid->Append( new wxPropertyCategory(_("Game's window")) );
    grid->Append( new wxUIntProperty(_("Width"), wxPG_LABEL, GetMainWindowDefaultWidth()) );
    grid->Append( new wxUIntProperty(_("Height"), wxPG_LABEL, GetMainWindowDefaultHeight()) );
    grid->Append( new wxBoolProperty(_("Vertical Synchronization"), wxPG_LABEL, IsVerticalSynchronizationEnabledByDefault()) );
    grid->Append( new wxBoolProperty(_("Limit the framerate"), wxPG_LABEL, GetMaximumFPS() != -1) );
    grid->Append( new wxIntProperty(_("Maximum FPS"), wxPG_LABEL, GetMaximumFPS()) );
    grid->Append( new wxUIntProperty(_("Minimum FPS"), wxPG_LABEL, GetMinimumFPS()) );

    grid->SetPropertyCell(_("Globals variables"), 1, _("Click to edit..."), wxNullBitmap, wxSystemSettings::GetColour(wxSYS_COLOUR_HOTLIGHT ));
    grid->SetPropertyReadOnly(_("Globals variables"));
    grid->SetPropertyCell(_("Extensions"), 1, _("Click to edit..."), wxNullBitmap, wxSystemSettings::GetColour(wxSYS_COLOUR_HOTLIGHT ));
    grid->SetPropertyReadOnly(_("Extensions"));

    if ( GetMaximumFPS() == -1 )
    {
        grid->GetProperty(_("Maximum FPS"))->Enable(false);
        grid->GetProperty(_("Maximum FPS"))->SetValue("");
    }
    else
        grid->GetProperty(_("Maximum FPS"))->Enable(true);
}

void Project::UpdateFromPropertyGrid(wxPropertyGrid * grid)
{
    if ( grid->GetProperty(_("Name of the project")) != NULL)
        SetName(gd::ToString(grid->GetProperty(_("Name of the project"))->GetValueAsString()));
    if ( grid->GetProperty(_("Author")) != NULL)
        SetAuthor(gd::ToString(grid->GetProperty(_("Author"))->GetValueAsString()));
    if ( grid->GetProperty(_("Width")) != NULL)
        SetMainWindowDefaultWidth(grid->GetProperty(_("Width"))->GetValue().GetInteger());
    if ( grid->GetProperty(_("Height")) != NULL)
        SetMainWindowDefaultHeight(grid->GetProperty(_("Height"))->GetValue().GetInteger());
    if ( grid->GetProperty(_("Vertical Synchronization")) != NULL)
        SetVerticalSyncActivatedByDefault(grid->GetProperty(_("Vertical Synchronization"))->GetValue().GetBool());
    if ( grid->GetProperty(_("Limit the framerate")) != NULL && !grid->GetProperty(_("Limit the framerate"))->GetValue().GetBool())
        SetMaximumFPS(-1);
    else if ( grid->GetProperty(_("Maximum FPS")) != NULL)
        SetMaximumFPS(grid->GetProperty(_("Maximum FPS"))->GetValue().GetInteger());
    if ( grid->GetProperty(_("Minimum FPS")) != NULL)
        SetMinimumFPS(grid->GetProperty(_("Minimum FPS"))->GetValue().GetInteger());
}

void Project::OnSelectionInPropertyGrid(wxPropertyGrid * grid, wxPropertyGridEvent & event)
{
    if ( event.GetColumn() == 1) //Manage button-like properties
    {
        if ( event.GetPropertyName() == _("Extensions") )
        {
            gd::ProjectExtensionsDialog dialog(NULL, *this);
            dialog.ShowModal();
        }
        else if ( event.GetPropertyName() == _("Globals variables") )
        {
            gd::ChooseVariableDialog dialog(NULL, GetVariables(), /*editingOnly=*/true);
            dialog.SetAssociatedProject(this);
            dialog.ShowModal();
        }
    }
}

void Project::OnChangeInPropertyGrid(wxPropertyGrid * grid, wxPropertyGridEvent & event)
{
    if (event.GetPropertyName() == _("Limit the framerate") )
        grid->EnableProperty(_("Maximum FPS"), grid->GetProperty(_("Limit the framerate"))->GetValue().GetBool());

    UpdateFromPropertyGrid(grid);
}

bool Project::SaveToFile(const std::string & filename)
{
    //Create document structure
    TiXmlDocument doc;
    TiXmlDeclaration* decl = new TiXmlDeclaration( "1.0", "ISO-8859-1", "" );
    doc.LinkEndChild( decl );

    TiXmlElement * root = new TiXmlElement( "Project" );
    doc.LinkEndChild( root );

    SaveToXml(root);

    //Wrie XML to file
    if ( !doc.SaveFile( filename.c_str() ) )
    {
        wxLogError( _( "Unable to save file to ")+filename+_("\nCheck that the drive has enough free space, is not write-protected and that you have read/write permissions." ) );
        return false;
    }

    return true;
}

bool Project::LoadFromFile(const std::string & filename)
{
    //Load document structure
    TiXmlDocument doc;
    if ( !doc.LoadFile(filename.c_str()) )
    {
        wxString errorTinyXmlDesc = doc.ErrorDesc();
        wxString error = _( "Error while loading :" ) + "\n" + errorTinyXmlDesc + "\n\n" +_("Make sure the file exists and that you have the right to open the file.");

        wxLogError( error );
        return false;
    }

    SetProjectFile(filename);

    TiXmlHandle hdl( &doc );
    LoadFromXml(hdl.FirstChildElement().Element());

    /*#if defined(GD_IDE_ONLY) //TODO
    if (!updateText.empty())
    {
        ProjectUpdateDlg updateDialog(NULL, updateText);
        updateDialog.ShowModal();
    }
    #endif*/
    return true;
}

bool Project::ValidateObjectName(const std::string & name)
{
    const std::vector < boost::shared_ptr<PlatformExtension> > extensions = GetPlatform().GetAllPlatformExtensions();

    //Check if name is not an expression
    bool nameUsedByExpression = (GetPlatform().GetInstructionsMetadataHolder().HasExpression(name) ||
                                 GetPlatform().GetInstructionsMetadataHolder().HasStrExpression(name));

    //Check if name is not an object expression
    for (unsigned int i = 0;i<extensions.size();++i)
    {
        if ( find(GetUsedPlatformExtensions().begin(),
                  GetUsedPlatformExtensions().end(),
                  extensions[i]->GetName()) == GetUsedPlatformExtensions().end() )
            continue; //Do not take care of unused extensions

        std::vector<std::string> objectsTypes = extensions[i]->GetExtensionObjectsTypes();

        for(unsigned int j = 0;j<objectsTypes.size();++j)
        {
            std::map<std::string, gd::ExpressionMetadata > allObjExpr = extensions[i]->GetAllExpressionsForObject(objectsTypes[j]);
            for(std::map<std::string, gd::ExpressionMetadata>::const_iterator it = allObjExpr.begin(); it != allObjExpr.end(); ++it)
            {
                if ( name == it->first )
                    nameUsedByExpression = true;
            }
        }
    }

    //Finally check if the name has only allowed characters
    std::string allowedCharacter = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
    return !(name.find_first_not_of(allowedCharacter) != std::string::npos || nameUsedByExpression);
}

std::string Project::GetBadObjectNameWarning()
{
    return gd::ToString(_("Please use only letters, digits\nand underscores ( _ ).\nName used by expressions\nare also forbidden."));
}

}