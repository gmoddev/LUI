#pragma once
#include <gtk/gtk.h>

// Slots have zero intrinsic minimum. Toolkit minimums may crop native content,
// but cannot enlarge canonical logical bounds or sibling layout.
GtkWidget* LuiGtkSurface();
GtkWidget* LuiGtkSlot(GtkWidget* Child);
void LuiGtkPlace(GtkWidget* Surface,GtkWidget* Slot);
void LuiGtkRect(GtkWidget* Slot,double X,double Y,double Width,double Height);
void LuiGtkClip(GtkWidget* Widget,bool HasClip,double X,double Y,double Width,double Height);
void LuiGtkDecoration(GtkWidget* Surface,bool Background,const GdkRGBA& BackgroundColor,const GdkRGBA& BorderColor,double BorderSize,double Radius);
GtkWidget* LuiGtkTextView();
void LuiGtkTextHint(GtkWidget* Widget,const char* Text);
