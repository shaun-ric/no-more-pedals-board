#include "Core/Graph.h"
#include <cstdio>
#include <cmath>
struct Pass : Node {
  int lat; bool in_=false,out_=false; int ch_=-1;
  Pass(std::string id, ParamRegistry& r, int lat_, bool i=false,bool o=false,int c=-1)
   : Node(id, id, "pass", r, true), lat(lat_), in_(i), out_(o), ch_(c) {}
  int latencySamples() const override { return lat; }
  bool isInput() const override { return in_; }
  bool isOutput() const override { return out_; }
  int deviceInputChannel() const override { return in_?ch_:-1; }
  int deviceOutputChannel() const override { return out_?ch_:-1; }
  std::vector<float> ring; int w=0;
  void process(juce::AudioBuffer<float>& b, int n) override {
    if(lat<=0) return;
    if(ring.empty()) ring.assign(lat+1,0.f);
    float* x=b.getWritePointer(0);
    for(int i=0;i<n;i++){ ring[w]=x[i]; if(++w>=(int)ring.size()) w=0; x[i]=ring[w]; }
  }
  void processInactive(juce::AudioBuffer<float>&, int) override {}
};
int main(){
  ParamRegistry reg; Graph g(reg);
  g.addNode(std::make_shared<Pass>("in",reg,0,true,false,0));
  g.addNode(std::make_shared<Pass>("slow",reg,100));
  g.addNode(std::make_shared<Pass>("fast",reg,0));
  g.addNode(std::make_shared<Pass>("out",reg,0,false,true,0));
  g.addNode(std::make_shared<Pass>("out2",reg,0,false,true,1));
  g.connect("in","slow"); g.connect("in","fast"); g.connect("slow","out"); g.connect("fast","out"); g.connect("fast","out2");
  g.prepare(48000,64);
  juce::String err; if(!g.commit(err)){printf("commit fail %s\n",err.toRawUTF8());return 1;}
  printf("compensated latency %d\n", g.compensatedLatency());
  // impulse
  std::vector<float> o0, o1;
  for(int b=0;b<5;b++){
    float inb[64]={0}, a[64]={0}, c[64]={0};
    if(b==0) inb[0]=1;
    const float* ins[1]={inb}; float* outs[2]={a,c};
    g.process(ins,1,outs,2,64);
    for(int i=0;i<64;i++){o0.push_back(a[i]);o1.push_back(c[i]);}
  }
  for(size_t i=0;i<o0.size();i++){ if(o0[i]!=0) printf("out  nonzero at %zu = %f\n",i,o0[i]); if(o1[i]!=0) printf("out2 nonzero at %zu = %f\n",i,o1[i]); }
}
