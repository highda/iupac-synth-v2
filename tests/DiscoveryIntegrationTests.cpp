#include "PluginProcessor.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

namespace { bool expect(bool c,const char*m){if(!c)std::cerr<<"discovery integration failed: "<<m<<'\n';return c;} }
int main(int argc,char**argv)
{
 if(argc!=4)return 2;
 ::setenv("IUPAC_CHEMISTRY_HELPER",argv[1],1);::setenv("IUPAC_DISCOVERY_INDEX",argv[2],1);::setenv("IUPAC_GENERATED_CACHE",argv[3],1);
 bool ok=true;IupacSynthProcessor browser;browser.prepareToPlay(48000,128);const auto before=iupac::domain::encodeStateJson(browser.snapshot());
 browser.searchDiscovery("hydrogen",true);browser.cancelDiscovery();ok&=expect(!browser.discoveryStatus().busy&&iupac::domain::encodeStateJson(browser.snapshot())==before,"cancelled search leaves sound and status stable");
 browser.searchDiscovery("eth",true);browser.searchDiscovery("gasotransmitter",false);for(int i=0;i<300&&browser.discoveryStatus().busy;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));auto found=browser.discoveryStatus();auto candidates=found.result.getProperty("candidates",{}).getArray();
 ok&=expect(found.result.getProperty("query",{}).toString()=="gasotransmitter","latest search replaces stale result");
 ok&=expect(candidates&&candidates->size()>=2,"ambiguous local name returns explicit bounded candidates");
 juce::var conflict(new juce::DynamicObject);conflict.getDynamicObject()->setProperty("validationStatus","conflict");conflict.getDynamicObject()->setProperty("canonicalIsomericSmiles","CCO");ok&=expect(!browser.applyDiscovery(conflict).empty(),"conflicting record cannot apply");ok&=expect(iupac::domain::encodeStateJson(browser.snapshot())==before,"rejected record preserves sound");
 auto selected=candidates->getReference(0).clone();const auto canonical=selected.getProperty("canonicalIsomericSmiles",{}).toString().toStdString();ok&=expect(browser.applyDiscovery(selected).empty(),"explicit validated selection starts generation");for(int i=0;i<1500&&browser.chemistryStatus().busy;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));auto selectedState=browser.snapshot();ok&=expect(selectedState.provenance.has_value(),"selected record generated normal State");
 IupacSynthProcessor direct;direct.prepareToPlay(48000,128);direct.applyChemistry(iupac::chemistry::InputMode::smiles,canonical);for(int i=0;i<1500&&direct.chemistryStatus().busy;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));ok&=expect(iupac::domain::encodePatchJson(selectedState.basePatch)==iupac::domain::encodePatchJson(direct.snapshot().basePatch),"browser and explicit structure use the same generation path");
 auto edited=selectedState;edited.controls.macros[0]=.73;edited.controls.outputGain=-11;edited.editedPatch.nodes[0].parameters[0].values[0]*=.9;ok&=expect(browser.loadState(edited).empty(),"generated result remains ordinarily editable");const auto expected=iupac::domain::encodeStateJson(browser.snapshot());auto preset=std::filesystem::path(argv[3]).parent_path()/"ordinary-user-preset.iupacpatch";ok&=expect(browser.saveStateFile(preset).empty(),"edited result saves as ordinary preset");
 browser.inspectGeneratedCache();for(int i=0;i<300&&browser.discoveryStatus().busy;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));auto cached=browser.discoveryStatus().result.getArray();ok&=expect(cached&&!cached->isEmpty(),"generated file cache is inspectable");
 if(cached&&!cached->isEmpty()){auto entry=cached->getReference(0).clone();::setenv("IUPAC_CHEMISTRY_HELPER","/missing/helper",1);ok&=expect(browser.applyDiscovery(entry).empty(),"cached State explicitly reopens without regeneration helper");}
 ok&=expect(browser.loadStateFile(preset).empty()&&iupac::domain::encodeStateJson(browser.snapshot())==expected,"ordinary preset restores exact edits and controls without helper");::setenv("IUPAC_CHEMISTRY_HELPER",argv[1],1);ok&=expect(browser.clearGeneratedCache().empty()&&std::filesystem::is_regular_file(preset),"cache clear cannot remove user preset");
 std::error_code ec;std::filesystem::remove(preset,ec);return ok?0:1;
}
