#include "EditorLookAndFeel.hpp"
namespace iupac::ui
{
juce::Font labelFont(float height){return juce::Font(juce::FontOptions(height,juce::Font::plain)).withExtraKerningFactor(0.08f);}
float editorScale(const juce::Component&c)noexcept{if(const auto*l=dynamic_cast<const EditorLookAndFeel*>(&c.getLookAndFeel()))return l->scale();return 1.0f;}
float scaledText(const juce::Component&c,float referenceText)noexcept{return(float)fontHeight(referenceText,editorScale(c));}
void drawCaption(juce::Graphics&g,juce::StringRef text,juce::Rectangle<int>area,juce::Justification just,float height){g.setFont(labelFont(height));g.drawText(juce::String(text).toUpperCase(),area,just,false);}
namespace
{
void strokeLine(juce::Graphics&g,juce::Rectangle<float>b,float x0,float y0,float x1,float y1){g.drawLine(b.getX()+x0*b.getWidth(),b.getY()+y0*b.getHeight(),b.getX()+x1*b.getWidth(),b.getY()+y1*b.getHeight(),hairline);}
juce::Path wave(juce::Rectangle<float>b,float cycles,float amplitude,float offset){juce::Path p;const int n=32;for(int i=0;i<=n;++i){const float t=(float)i/n;const float y=offset-amplitude*std::sin(t*cycles*juce::MathConstants<float>::twoPi);const auto pt=juce::Point<float>(b.getX()+t*b.getWidth(),b.getY()+y*b.getHeight());if(i)p.lineTo(pt);else p.startNewSubPath(pt);}return p;}
}
void drawGlyph(juce::Graphics&g,std::string_view id,juce::Rectangle<float>b,juce::Colour c)
{
 g.setColour(c);const juce::PathStrokeType stroke(hairline,juce::PathStrokeType::curved,juce::PathStrokeType::rounded);
 if(id=="harmonic"||id=="SRC"){for(int i=0;i<5;++i){const float x=0.1f+0.2f*(float)i,h=1.0f-0.85f/(float)(i+1);strokeLine(g,b,x,1.0f,x,h);}strokeLine(g,b,0.02f,1.0f,0.98f,1.0f);}
 else if(id=="fm"){g.strokePath(wave(b,1.0f,0.35f,0.5f),stroke);g.strokePath(wave(b,4.0f,0.12f,0.5f),stroke);}
 else if(id=="noise"){juce::Path p;std::uint32_t r=7;for(int i=0;i<=12;++i){r=r*1664525u+1013904223u;const float y=0.15f+0.7f*(float)(r>>8&0xffff)/65535.0f;const auto pt=juce::Point<float>(b.getX()+b.getWidth()*(float)i/12.0f,b.getY()+b.getHeight()*y);if(i)p.lineTo(pt);else p.startNewSubPath(pt);}g.strokePath(p,stroke);}
 else if(id=="resonator"||id=="RES"){const auto centre=b.getCentre();for(float r=0.18f;r<=0.5f;r+=0.16f)g.drawEllipse(centre.x-r*b.getWidth(),centre.y-r*b.getHeight(),2*r*b.getWidth(),2*r*b.getHeight(),hairline);}
 else if(id=="filter"||id=="FILT"){juce::Path p;p.startNewSubPath(b.getX(),b.getY()+b.getHeight()*0.3f);p.lineTo(b.getX()+b.getWidth()*0.5f,b.getY()+b.getHeight()*0.3f);p.quadraticTo(b.getX()+b.getWidth()*0.62f,b.getY()+b.getHeight()*0.3f,b.getX()+b.getWidth()*0.66f,b.getY()+b.getHeight()*0.1f);p.quadraticTo(b.getX()+b.getWidth()*0.7f,b.getY()+b.getHeight()*0.4f,b.getRight(),b.getBottom());g.strokePath(p,stroke);}
 else if(id=="shaper"||id=="SHAPE"){juce::Path p;p.startNewSubPath(b.getX(),b.getBottom());p.cubicTo(b.getX()+b.getWidth()*0.15f,b.getBottom(),b.getX()+b.getWidth()*0.2f,b.getY()+b.getHeight()*0.15f,b.getCentreX(),b.getCentreY());p.cubicTo(b.getX()+b.getWidth()*0.8f,b.getBottom()-b.getHeight()*0.15f,b.getX()+b.getWidth()*0.85f,b.getY(),b.getRight(),b.getY());g.strokePath(p,stroke);}
 else if(id=="mixer"||id=="MIX"){strokeLine(g,b,0.0f,0.1f,0.6f,0.5f);strokeLine(g,b,0.0f,0.5f,0.6f,0.5f);strokeLine(g,b,0.0f,0.9f,0.6f,0.5f);strokeLine(g,b,0.6f,0.5f,1.0f,0.5f);}
 else if(id=="OUT"){juce::Path p;p.startNewSubPath(b.getX(),b.getY()+b.getHeight()*0.35f);p.lineTo(b.getX()+b.getWidth()*0.35f,b.getY()+b.getHeight()*0.35f);p.lineTo(b.getX()+b.getWidth()*0.7f,b.getY());p.lineTo(b.getX()+b.getWidth()*0.7f,b.getBottom());p.lineTo(b.getX()+b.getWidth()*0.35f,b.getY()+b.getHeight()*0.65f);p.lineTo(b.getX(),b.getY()+b.getHeight()*0.65f);p.closeSubPath();g.strokePath(p,stroke);strokeLine(g,b,0.85f,0.3f,0.95f,0.5f);strokeLine(g,b,0.95f,0.5f,0.85f,0.7f);}
 else if(id=="new"){juce::Path p;p.startNewSubPath(b.getX()+b.getWidth()*0.2f,b.getY());p.lineTo(b.getX()+b.getWidth()*0.65f,b.getY());p.lineTo(b.getRight()-b.getWidth()*0.15f,b.getY()+b.getHeight()*0.25f);p.lineTo(b.getRight()-b.getWidth()*0.15f,b.getBottom());p.lineTo(b.getX()+b.getWidth()*0.2f,b.getBottom());p.closeSubPath();g.strokePath(p,stroke);strokeLine(g,b,0.65f,0.0f,0.65f,0.25f);strokeLine(g,b,0.65f,0.25f,0.85f,0.25f);}
 else if(id=="load"){strokeLine(g,b,0.5f,0.9f,0.5f,0.1f);strokeLine(g,b,0.5f,0.1f,0.25f,0.4f);strokeLine(g,b,0.5f,0.1f,0.75f,0.4f);strokeLine(g,b,0.1f,1.0f,0.9f,1.0f);}
 else if(id=="save"){strokeLine(g,b,0.5f,0.1f,0.5f,0.75f);strokeLine(g,b,0.5f,0.75f,0.25f,0.5f);strokeLine(g,b,0.5f,0.75f,0.75f,0.5f);strokeLine(g,b,0.1f,1.0f,0.9f,1.0f);}
 else if(id=="preset"){juce::Path p;p.startNewSubPath(b.getX()+b.getWidth()*0.2f,b.getY());p.lineTo(b.getRight()-b.getWidth()*0.2f,b.getY());p.lineTo(b.getRight()-b.getWidth()*0.2f,b.getBottom());p.lineTo(b.getCentreX(),b.getBottom()-b.getHeight()*0.3f);p.lineTo(b.getX()+b.getWidth()*0.2f,b.getBottom());p.closeSubPath();g.strokePath(p,stroke);}
 else if(id=="reset"||id=="resetControls"){juce::Path p;p.addCentredArc(b.getCentreX(),b.getCentreY(),b.getWidth()*0.4f,b.getHeight()*0.4f,0.0f,0.6f,juce::MathConstants<float>::twoPi-0.4f,true);g.strokePath(p,stroke);strokeLine(g,b,0.72f,0.05f,0.72f,0.35f);strokeLine(g,b,0.72f,0.35f,0.98f,0.35f);if(id=="resetControls")g.fillEllipse(b.getCentreX()-1.5f,b.getCentreY()-1.5f,3.0f,3.0f);}
 else if(id=="chemistry"){juce::Path p;p.addPolygon(b.getCentre(),6,b.getWidth()*0.45f,0.0f);g.strokePath(p,stroke);strokeLine(g,b,0.5f,0.5f,0.5f,0.1f);strokeLine(g,b,0.5f,0.5f,0.85f,0.7f);strokeLine(g,b,0.5f,0.5f,0.15f,0.7f);}
 else if(id=="remove"){strokeLine(g,b,0.2f,0.2f,0.8f,0.8f);strokeLine(g,b,0.8f,0.2f,0.2f,0.8f);}
 else if(id=="add"){strokeLine(g,b,0.5f,0.1f,0.5f,0.9f);strokeLine(g,b,0.1f,0.5f,0.9f,0.5f);}
 else if(id=="edit"){strokeLine(g,b,0.1f,0.25f,0.9f,0.25f);strokeLine(g,b,0.1f,0.5f,0.9f,0.5f);strokeLine(g,b,0.1f,0.75f,0.9f,0.75f);strokeLine(g,b,0.4f,0.1f,0.4f,0.9f);}
}
EditorLookAndFeel::EditorLookAndFeel()
{
 setColour(juce::ResizableWindow::backgroundColourId,ground);setColour(juce::TextButton::buttonColourId,ground);setColour(juce::TextButton::textColourOffId,ink);setColour(juce::TextButton::textColourOnId,ground);setColour(juce::TextButton::buttonOnColourId,ink);
 setColour(juce::ComboBox::backgroundColourId,ground);setColour(juce::ComboBox::textColourId,ink);setColour(juce::ComboBox::outlineColourId,ink);setColour(juce::ComboBox::arrowColourId,ink);setColour(juce::ComboBox::focusedOutlineColourId,ink);
 setColour(juce::PopupMenu::backgroundColourId,ground);setColour(juce::PopupMenu::textColourId,ink);setColour(juce::PopupMenu::highlightedBackgroundColourId,ink);setColour(juce::PopupMenu::highlightedTextColourId,ground);
 setColour(juce::Label::textColourId,ink);setColour(juce::Label::backgroundColourId,juce::Colours::transparentBlack);setColour(juce::Label::outlineColourId,juce::Colours::transparentBlack);
 setColour(juce::TextEditor::backgroundColourId,ground);setColour(juce::TextEditor::textColourId,ink);setColour(juce::TextEditor::outlineColourId,ink);setColour(juce::TextEditor::focusedOutlineColourId,ink);setColour(juce::TextEditor::highlightColourId,ink.withAlpha(0.2f));setColour(juce::TextEditor::highlightedTextColourId,ink);setColour(juce::CaretComponent::caretColourId,ink);
 setColour(juce::ScrollBar::thumbColourId,ink);setColour(juce::ScrollBar::trackColourId,ground);setColour(juce::ScrollBar::backgroundColourId,ground);
 setColour(juce::ToggleButton::textColourId,ink);setColour(juce::ToggleButton::tickColourId,ink);setColour(juce::ToggleButton::tickDisabledColourId,ink.withAlpha(0.4f));
 setColour(juce::ListBox::backgroundColourId,ground);setColour(juce::ListBox::textColourId,ink);setColour(juce::ListBox::outlineColourId,ink);
 setColour(juce::MidiKeyboardComponent::whiteNoteColourId,ground);setColour(juce::MidiKeyboardComponent::blackNoteColourId,ink);setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId,ink);setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,ink.withAlpha(0.15f));setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId,accent.withAlpha(0.6f));setColour(juce::MidiKeyboardComponent::textLabelColourId,ink);setColour(juce::MidiKeyboardComponent::shadowColourId,juce::Colours::transparentBlack);setColour(juce::MidiKeyboardComponent::upDownButtonBackgroundColourId,ground);setColour(juce::MidiKeyboardComponent::upDownButtonArrowColourId,ink);
 setColour(juce::DocumentWindow::backgroundColourId,ground);setColour(juce::DocumentWindow::textColourId,ink);setColour(juce::TooltipWindow::backgroundColourId,ground);setColour(juce::TooltipWindow::textColourId,ink);setColour(juce::TooltipWindow::outlineColourId,ink);
 setColour(juce::AlertWindow::backgroundColourId,ground);setColour(juce::AlertWindow::textColourId,ink);setColour(juce::AlertWindow::outlineColourId,ink);
 setColour(juce::FileBrowserComponent::currentPathBoxBackgroundColourId,ground);setColour(juce::FileBrowserComponent::currentPathBoxTextColourId,ink);setColour(juce::FileBrowserComponent::filenameBoxBackgroundColourId,ground);setColour(juce::FileBrowserComponent::filenameBoxTextColourId,ink);setColour(juce::DirectoryContentsDisplayComponent::highlightColourId,ink);setColour(juce::DirectoryContentsDisplayComponent::textColourId,ink);setColour(juce::DirectoryContentsDisplayComponent::highlightedTextColourId,ground);
}
// The caps are reference-frame heights scaled by the live editor scale (#89): the control heights these read grow
// with the window, so without a scaled cap every label froze at 11 px however large the editor became.
juce::Font EditorLookAndFeel::getTextButtonFont(juce::TextButton&,int h){return labelFont(juce::jlimit(minimumTextHeight,scaled(11.0f),(float)h*0.42f));}
juce::Font EditorLookAndFeel::getComboBoxFont(juce::ComboBox&b){return labelFont(juce::jlimit(minimumTextHeight,scaled(11.0f),(float)b.getHeight()*0.5f));}
juce::Font EditorLookAndFeel::getPopupMenuFont(){return labelFont(scaled(11.0f));}
juce::Font EditorLookAndFeel::getLabelFont(juce::Label&l){return labelFont(juce::jlimit(minimumTextHeight,scaled(11.0f),(float)l.getHeight()*0.55f));}
void EditorLookAndFeel::drawButtonBackground(juce::Graphics&g,juce::Button&b,const juce::Colour&,bool over,bool down)
{
 auto r=b.getLocalBounds().toFloat().reduced(0.5f);const bool on=b.getToggleState()||down;g.setColour(on?ink:ground);g.fillRoundedRectangle(r,2.0f);g.setColour(ink);g.drawRoundedRectangle(r,2.0f,hairline);if(over&&!on){g.setColour(ink.withAlpha(0.08f));g.fillRoundedRectangle(r,2.0f);}
}
void EditorLookAndFeel::drawButtonText(juce::Graphics&g,juce::TextButton&b,bool,bool down)
{
 const bool on=b.getToggleState()||down;g.setColour(on?ground:ink);auto r=b.getLocalBounds();const auto id=b.getComponentID();
 if(id.isNotEmpty()){const int s=juce::jmin(r.getHeight()-8,juce::roundToInt(scaled(12.0f)));drawGlyph(g,id.toStdString(),juce::Rectangle<float>((float)r.getX()+6.0f,(float)r.getCentreY()-(float)s/2.0f,(float)s,(float)s),on?ground:ink);r.removeFromLeft(s+8);g.setColour(on?ground:ink);}
 g.setFont(getTextButtonFont(b,b.getHeight()));g.drawText(b.getButtonText().toUpperCase(),r.reduced(4,0),id.isNotEmpty()?juce::Justification::centredLeft:juce::Justification::centred,false);
}
void EditorLookAndFeel::drawComboBox(juce::Graphics&g,int w,int h,bool,int,int,int,int,juce::ComboBox&)
{
 auto r=juce::Rectangle<float>(0,0,(float)w,(float)h).reduced(0.5f);g.setColour(ground);g.fillRoundedRectangle(r,2.0f);g.setColour(ink);g.drawRoundedRectangle(r,2.0f,hairline);
 const float a=scaled(3.0f)/3.0f;// the arrow keeps its proportion to the text inside the box (#89)
 const float ax=(float)w-3.0f*a,ay=(float)h*0.5f;juce::Path p;p.startNewSubPath(ax-a,ay-a*0.5f);p.lineTo(ax,ay+a*0.5f);p.lineTo(ax+a,ay-a*0.5f);g.strokePath(p,juce::PathStrokeType(hairline));
}
void EditorLookAndFeel::positionComboBoxText(juce::ComboBox&b,juce::Label&l){l.setBounds(2,0,b.getWidth()-juce::roundToInt(scaled(16.0f)),b.getHeight());l.setFont(getComboBoxFont(b));}
void EditorLookAndFeel::drawPopupMenuBackground(juce::Graphics&g,int w,int h){g.fillAll(ground);g.setColour(ink);g.drawRect(0,0,w,h,(int)hairline);}
void EditorLookAndFeel::drawPopupMenuItem(juce::Graphics&g,const juce::Rectangle<int>&area,bool separator,bool active,bool highlighted,bool ticked,bool,const juce::String&text,const juce::String&,const juce::Drawable*,const juce::Colour*)
{
 if(separator){g.setColour(ink);g.fillRect(area.reduced(6,0).withHeight(1).withY(area.getCentreY()));return;}
 if(highlighted&&active){g.setColour(ink);g.fillRect(area);}g.setColour(highlighted&&active?ground:ink.withAlpha(active?1.0f:0.4f));g.setFont(getPopupMenuFont());const int pad=juce::roundToInt(scaled(8.0f));auto r=area.reduced(pad,0);if(ticked){const float dot=scaled(4.0f);g.fillEllipse((float)r.getX(),(float)r.getCentreY()-dot*0.5f,dot,dot);}r.removeFromLeft(pad);g.drawText(text.toUpperCase(),r,juce::Justification::centredLeft,true);
}
void EditorLookAndFeel::drawScrollbar(juce::Graphics&g,juce::ScrollBar&,int x,int y,int w,int h,bool vertical,int start,int size,bool,bool)
{
 g.setColour(ink.withAlpha(0.15f));g.fillRect(x,y,w,h);g.setColour(ink);if(vertical)g.fillRect(x+1,start,juce::jmax(1,w-2),size);else g.fillRect(start,y+1,size,juce::jmax(1,h-2));
}
void EditorLookAndFeel::fillTextEditorBackground(juce::Graphics&g,int w,int h,juce::TextEditor&){g.setColour(ground);g.fillRect(0,0,w,h);}
void EditorLookAndFeel::drawTextEditorOutline(juce::Graphics&g,int w,int h,juce::TextEditor&){g.setColour(ink);g.drawRect(0,0,w,h,(int)hairline);}
void EditorLookAndFeel::drawLabel(juce::Graphics&g,juce::Label&l){if(l.isBeingEdited())return;g.setColour(l.findColour(juce::Label::textColourId).withMultipliedAlpha(l.isEnabled()?1.0f:0.5f));g.setFont(getLabelFont(l));g.drawFittedText(l.getText().toUpperCase(),l.getBorderSize().subtractedFrom(l.getLocalBounds()),l.getJustificationType(),1,1.0f);}
void EditorLookAndFeel::drawToggleButton(juce::Graphics&g,juce::ToggleButton&b,bool,bool){auto r=b.getLocalBounds().toFloat().reduced(0.5f);g.setColour(b.getToggleState()?ink:ground);g.fillRoundedRectangle(r,2.0f);g.setColour(ink);g.drawRoundedRectangle(r,2.0f,hairline);g.setColour(b.getToggleState()?ground:ink);g.setFont(labelFont(juce::jlimit(minimumTextHeight,scaled(10.0f),(float)b.getHeight()*0.45f)));g.drawText(b.getButtonText().toUpperCase(),b.getLocalBounds(),juce::Justification::centred,false);}
void EditorLookAndFeel::drawDocumentWindowTitleBar(juce::DocumentWindow&w,juce::Graphics&g,int width,int h,int,int,const juce::Image*,bool){g.fillAll(ground);g.setColour(ink);g.fillRect(0,h-1,width,1);drawCaption(g,w.getName(),juce::Rectangle<int>(12,0,width-24,h),juce::Justification::centredLeft,scaled(11.0f));}
void EditorLookAndFeel::drawCallOutBoxBackground(juce::CallOutBox&,juce::Graphics&g,const juce::Path&path,juce::Image&){g.setColour(ground);g.fillPath(path);g.setColour(ink);g.strokePath(path,juce::PathStrokeType(hairline));}
juce::Button*EditorLookAndFeel::createDocumentWindowButton(int type){auto*b=new juce::TextButton(type==juce::DocumentWindow::closeButton?"":type==juce::DocumentWindow::minimiseButton?"min":"max");if(type==juce::DocumentWindow::closeButton)b->setComponentID("remove");return b;}
}
