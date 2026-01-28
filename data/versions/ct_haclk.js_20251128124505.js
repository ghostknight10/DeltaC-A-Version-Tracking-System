function pasteToCodeMirror(text) {
  let cmNode = document.querySelector(".cm-content")?.cmView 
            || document.querySelector(".cm-content")?.parentElement?.cmView;
  if (!cmNode) {
    console.error("CodeMirror view not found");
    return;
  }
  let view = cmNode.view;
  view.dispatch({
    changes: { from: 0, insert: text }
  });
}

// Example usage:
pasteToCodeMirror(`
  a,b= map(int,input().split())
def GCD(a,b):
	_min=a if a<b else b
    _max=a if a>b else b
    r=_max%_min
    if _min!=0 and _max!=0:
        if r==0:return _min
    elif _min == 0 and _max!= 0: return _max
    else: return 0
print(GCD(a,b))
  `);
  