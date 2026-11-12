#include "Core/Graph.h"
#include <cstdio>
struct M : Node {
  bool in_,out_;
  M(std::string id, ParamRegistry& r,bool i=false,bool o=false):Node(id,id,"m",r,true),in_(i),out_(o){}
  bool isInput() const override {return in_;} bool isOutput() const override {return out_;}
  int deviceInputChannel() const override {return in_?0:-1;} int deviceOutputChannel() const override {return out_?0:-1;}
  void process(juce::AudioBuffer<float>& b,int) override { b.clear(); }
};
int main(){ ParamRegistry reg; Graph g(reg);
  g.addNode(std::make_shared<M>("in",reg,true)); g.addNode(std::make_shared<M>("out",reg,false,true)); g.addNode(std::make_shared<M>("floating",reg));
  g.connect("in","out"); g.prepare(48000,64); juce::String e; bool ok=g.commit(e); printf("commit %d '%s'\n",ok,e.toRawUTF8());
  float in[64]={0},o[64]={0}; const float* ins[1]={in}; float* outs[1]={o}; g.process(ins,1,outs,1,64); printf("ok\n"); }
