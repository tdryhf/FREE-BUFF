const {chromium}=require('playwright');
const S=process.argv[2]||'..';  // writes S/shots/*.png
require('fs').mkdirSync(S+'/shots',{recursive:true});
(async()=>{const b=await chromium.launch();const errs=[];
 for(const scheme of ['light','dark']){
  const ctx=await b.newContext({viewport:{width:390,height:844},deviceScaleFactor:2,colorScheme:scheme,reducedMotion:'reduce'});
  const p=await ctx.newPage();p.on('console',m=>{if(m.type()==='error')errs.push(scheme+': '+m.text())});p.on('pageerror',e=>errs.push(scheme+' PAGEERR: '+e.message));
  for(const st of ['open','caution','closed','emergency','check','fast']){
   await p.request.get('http://localhost:8099/set?s='+st);
   await p.goto('http://localhost:8099/');await p.waitForTimeout(1800);
   const ov=await p.evaluate(()=>document.documentElement.scrollWidth);
   if(ov>390)errs.push(`${scheme} ${st}: horizontal overflow ${ov}`);
   await p.screenshot({path:`${S}/shots/${st}-${scheme}.png`,fullPage:true});
  }
  await ctx.close()}
 console.log(errs.join('\n')||'no errors');await b.close()})();
