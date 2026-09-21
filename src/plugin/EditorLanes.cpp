#include "EditorLanes.hpp"
namespace iupac::ui
{
juce::String modulationSourceName(domain::ModulationSource s){static constexpr std::array n{"E1","E2","E3","L1","L2","VEL","KEY","BEND","CC1","MACRO 1","MACRO 2","MACRO 3","MACRO 4","E4"};return n[(std::size_t)juce::jlimit(0,(int)domain::modulationSourceCount-1,(int)s)];}
std::vector<LaneDestination>laneDestinations(const domain::Patch&p)
{
 std::vector<LaneDestination>d;const auto map=assignSlots(p);
 for(std::size_t s=0;s<moduleSlotCount;++s){if(map.node[s]<0)continue;const auto&n=p.nodes[(std::size_t)map.node[s]];const auto&m=domain::moduleCatalog()[(std::size_t)n.type];for(const auto&pd:m.parameters)if(pd.modulatable)d.push_back({n.id,std::string(pd.id),juce::String(slotKindName(slotTable[s].kind).data())+juce::String(slotTable[s].instance+1)+" · "+juce::String(pd.id.data()).toUpperCase()});}
 return d;
}
LaneView::LaneView(LaneMatrix&m,std::size_t row):matrix_(m),row_(row),depth_(domain::ParameterDescriptor{"depth","",-1,1,0,domain::ParameterScale::linear,domain::ParameterKind::continuous,false,0,20.0,{}},false,true)
{
 setName("Lane "+juce::String((int)row+1));for(int i=0;i<(int)domain::modulationSourceCount;++i)source_.addItem(modulationSourceName((domain::ModulationSource)i),i+1);source_.setName("Lane source");destination_.setName("Lane destination");depth_.setName("Lane depth");depth_.setStep(0.01);enable_.setClickingTogglesState(true);enable_.setName("Lane enable");remove_.setName("Lane remove");
 source_.onChange=[this]{changed();};destination_.onChange=[this]{changed();};depth_.onChange=[this](double){changed();};enable_.onClick=[this]{changed();};remove_.onClick=[this]{if(matrix_.onRemove)matrix_.onRemove(row_);};
 for(auto*c:std::initializer_list<juce::Component*>{&source_,&destination_,&depth_,&enable_,&remove_})addAndMakeVisible(c);
}
void LaneView::resized()
{
 // Minimum widths follow the editor scale (#89) so the combo-box text inside them grows with the window.
 const float s=editorScale(*this);auto px=[s](int reference){return juce::roundToInt((float)reference*s);};
 auto r=getLocalBounds().reduced(2);r.removeFromLeft(px(4));remove_.setBounds(r.removeFromRight(r.getHeight()));r.removeFromRight(px(4));enable_.setBounds(r.removeFromRight(px(30)));r.removeFromRight(px(6));
 source_.setBounds(r.removeFromLeft(juce::jmax(px(60),r.getWidth()/6)));r.removeFromLeft(px(6));destination_.setBounds(r.removeFromLeft(juce::jmax(px(100),r.getWidth()*2/5)));r.removeFromLeft(px(8));depth_.setBounds(r);
}
void LaneView::paint(juce::Graphics&g){g.setColour(ink.withAlpha(0.25f));g.drawLine(0,(float)getHeight()-0.5f,(float)getWidth(),(float)getHeight()-0.5f,hairline);if(selected_){g.setColour(accent);g.fillRect(0,2,3,getHeight()-4);}}
void LaneView::mouseDown(const juce::MouseEvent&){matrix_.select(row_);}
void LaneView::sync(const domain::MatrixRow&row,const std::vector<LaneDestination>&destinations)
{
 syncing_=true;value_=row;destinations_=destinations;source_.setSelectedId((int)row.source+1,juce::dontSendNotification);destination_.clear(juce::dontSendNotification);int selected=0;
 for(std::size_t i=0;i<destinations_.size();++i){destination_.addItem(destinations_[i].label,(int)i+1);if(destinations_[i].nodeId==row.destinationNode&&destinations_[i].parameter==row.destinationParameter)selected=(int)i+1;}
 destination_.setSelectedId(selected,juce::dontSendNotification);if(selected==0)destination_.setText(juce::String(row.destinationNode)+" · "+juce::String(row.destinationParameter).toUpperCase(),juce::dontSendNotification);
 depth_.setValue(row.depth,false);enable_.setToggleState(row.enabled,juce::dontSendNotification);syncing_=false;
}
domain::MatrixRow LaneView::row()const
{
 auto r=value_;r.source=(domain::ModulationSource)juce::jlimit(0,(int)domain::modulationSourceCount-1,source_.getSelectedId()-1);const int d=destination_.getSelectedId();if(d>0&&(std::size_t)d<=destinations_.size()){r.destinationNode=destinations_[(std::size_t)d-1].nodeId;r.destinationParameter=destinations_[(std::size_t)d-1].parameter;}r.depth=depth_.value();r.enabled=enable_.getToggleState();return r;
}
void LaneView::changed(){if(syncing_)return;matrix_.select(row_);if(matrix_.onEdit)matrix_.onEdit(row_,row());}
LaneMatrix::LaneMatrix(){setName("Lane matrix");add_.setName("Add lane");add_.onClick=[this]{if(onAdd)onAdd();};addAndMakeVisible(add_);viewport_.setViewedComponent(&content_,false);viewport_.setScrollBarsShown(true,false);addAndMakeVisible(viewport_);}
void LaneMatrix::resized(){const float s=editorScale(*this);auto r=getLocalBounds();auto top=r.removeFromTop(headerHeight());add_.setBounds(top.removeFromRight(juce::roundToInt(70.0f*s)).reduced(0,2));viewport_.setBounds(r);const int h=juce::roundToInt(22.0f*s);content_.setSize(viewport_.getMaximumVisibleWidth(),(int)lanes_.size()*h);for(std::size_t i=0;i<lanes_.size();++i)lanes_[i]->setBounds(0,(int)i*h,content_.getWidth(),h);}
void LaneMatrix::paint(juce::Graphics&g){g.setColour(ink);const int header=headerHeight();drawCaption(g,"modulation lanes",juce::Rectangle<int>(4,0,juce::roundToInt(200.0f*editorScale(*this)),header),juce::Justification::centredLeft,scaledText(*this,9.0f));g.drawLine(0,(float)header-0.5f,(float)getWidth(),(float)header-0.5f,hairline);}
void LaneMatrix::setPatch(const domain::Patch&p)
{
 destinations_=laneDestinations(p);while(lanes_.size()>p.matrix.size())lanes_.pop_back();while(lanes_.size()<p.matrix.size()){lanes_.push_back(std::make_unique<LaneView>(*this,lanes_.size()));content_.addAndMakeVisible(*lanes_.back());}
 for(std::size_t i=0;i<lanes_.size();++i)lanes_[i]->sync(p.matrix[i],destinations_);if(selected_&&*selected_>=lanes_.size())selected_.reset();for(std::size_t i=0;i<lanes_.size();++i)lanes_[i]->setSelected(selected_==i);resized();
}
void LaneMatrix::select(std::optional<std::size_t>i){if(i&&*i>=lanes_.size())i.reset();selected_=i;for(std::size_t k=0;k<lanes_.size();++k)lanes_[k]->setSelected(selected_==k);if(onSelect)onSelect(selected_);}
}
