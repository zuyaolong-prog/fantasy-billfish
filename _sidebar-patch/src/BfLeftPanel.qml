import QtQuick 2.12
import QtQuick.Controls 1.4
import QtQml.Models 2.15
import QtQuick.Layouts 1.15
import SkinSetting 1.0

import "./BfFolderView"
import "./BfTagView"
Item
{
    property color background_color: skin_model.getColor(BFSkinSetting.KCommonBackgroundColor)+skin_model.color_change_sign
    property color splitter_color: skin_model.getColor(BFSkinSetting.KCommonSplitterLineColor)+skin_model.color_change_sign
    property color second_color: skin_model.getColor(BFSkinSetting.KCommonSecondaryColor)+skin_model.color_change_sign
    property color font_number_color: skin_model.getColor(BFSkinSetting.KCommonAuxiliaryColor)+skin_model.color_change_sign

    readonly property int  tag_min_height: 35
    readonly property int  folder_min_height: 30
    
    
    Rectangle
    {
        id:split_rect

        anchors.fill: parent


        SplitView {
            anchors.fill: parent
            orientation: Qt.Vertical

            handleDelegate:
                Rectangle{
                id: splite
                width:parent.width
                height: 4
                color: background_color

                Rectangle
                {
                    id: top_splite
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    width: split_rect.width
                    color: styleData.hovered?font_number_color:splitter_color
                    height: 1
                }


                Rectangle
                {
                    id:bottom_splite
                    color: styleData.hovered?font_number_color:splitter_color
                    anchors.left: parent.left
                    anchors.leftMargin: parent.width/2-5
                    anchors.top: top_splite.bottom
                    anchors.topMargin: 2
                    width:10
                    height: 1
                }
            }


            Rectangle {
                Layout.minimumHeight: folder_min_height
                Layout.maximumHeight: split_rect.height
Layout.fillHeight:true
                height:control.tree_view_height
                id:folder_rect
                color: background_color
                BfFolderTreeView{
                    id: folder_tree_view
                    anchors.fill: parent
                }
                
                onHeightChanged: {
                    control.setFolderHeight(folder_rect.height);
                }

                Rectangle {
                    height:folder_rect.height
                    width:folder_rect.width-3
                    color:Qt.rgba(79/255, 125/255, 1, 0.3)
                    anchors.left: folder_rect.left
                    anchors.top: folder_rect.top
                    border.color: "#4F7DFF"
                    visible: control.guider_mask_view_open
                    border.width: 1
                    radius: 2
                }
            }

            Rectangle {
                id: label_rect
                visible:false
enabled:false
Layout.minimumHeight:0
Layout.maximumHeight:0
                height:0
                color: background_color
                BfTagTreeView{
                    id: tag_tree_view
                    anchors.fill: parent
                }
            }
        }
    }


    Connections
    {
        target: folder_tree_view

        function onSignSelection()
        {
            tag_tree_view.clearSelect()

            tag_tree_view.tag_mode_btn =true
        }
        
        function onSignNoSelection()
        {
            tag_tree_view.tag_mode_btn =false
        }
        
        function onSignRightPress()
        {
              tag_tree_view.right_index = undefined
        }
    }


    Connections
    {
        target: tag_tree_view

        function onSignSelection()
        {
            folder_tree_view.clearSelect()
            folder_tree_view.clearCurrentFolder()
            tag_tree_view.tag_mode_btn =false
        }
        
        function onSignRightPress()
        {
              folder_tree_view.right_index = undefined
        }
      
    }

    Connections
    {
        target: control

        function onSignCopyTags()
        {
            tag_tree_view.copyTags()
        }
        
        
        function onSignSearch()
        {
            folder_tree_view.searchFolder()
            tag_tree_view.searchTag()
        }
        
        function onSignAdd()
        {
               folder_tree_view.addFolder()
               tag_tree_view.addTag()
        }
        
        function onSignAddSub()
        {
               folder_tree_view.addSubFolder()
               tag_tree_view.addSubTag()
        }
        
        function onSignDelete()
        {
               folder_tree_view.delFolder()
               tag_tree_view.delTag()
              
        }
        
        function onSignRename()
        {
            folder_tree_view.renameFolder()
            tag_tree_view.renameTag()
        }
        
        function onSignMoveToCloud()
        {
            folder_tree_view.moveToCloud()
        }
        
        
        function onSignReanalyse()
        {
            folder_tree_view.reAnalysis()
        }
        
        function onSignScanrepeat()
        {
            folder_tree_view.scanRepate()
            tag_tree_view.tag_mode_btn =false
        }

        function onSignOpenFolder()
        {
            folder_tree_view.openFolder()
        }
        
        function onSignExpandCollapseSibling()
        {
            folder_tree_view.expandCollapseSibling()
            tag_tree_view.expandCollapseSibling()
        }
        
        function onSignExpandCollapseAll()
        {
            folder_tree_view.expandCollapseAll()
            tag_tree_view.expandCollapseAll()
        }
        
    }
}
