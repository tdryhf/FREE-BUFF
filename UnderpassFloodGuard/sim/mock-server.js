// Mock ESP32: serves the sketch's page + JSON dumped by the simulator.
const http=require('http'),fs=require('fs'),path=require('path');
const D=process.argv[2];let state='open',down=false;
const rd=f=>fs.readFileSync(path.join(D,f),'utf8');
http.createServer((q,r)=>{const u=new URL(q.url,'http://x');
 if(u.pathname==='/set'){state=u.searchParams.get('s');down=state==='down';r.end('ok');return}
 if(u.pathname==='/'){r.setHeader('content-type','text/html; charset=utf-8');r.end(rd('page2.html'));return}
 if(u.pathname==='/sign'){r.setHeader('content-type','text/html; charset=utf-8');r.end(rd('sign2.html'));return}
 if(down){q.socket.destroy();return}
 if(u.pathname==='/info'){r.setHeader('content-type','application/json');r.end(rd('info2.json'));return}
 if(u.pathname==='/data'){r.setHeader('content-type','application/json');r.end(rd('t2_'+state+'.json'));return}
 if(u.pathname==='/cmd'){r.end('{"ok":1}');return}
 r.statusCode=404;r.end()}).listen(8099,()=>console.log('mock on 8099'));
