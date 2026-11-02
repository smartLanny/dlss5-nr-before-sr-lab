"""Exercise the actual old/new receipt AST expressions on the frozen array; CPU only."""
from pathlib import Path
import ast,hashlib,json,re,struct
ROOT=Path(__file__).resolve().parent
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def expression(path,target):
 tree=ast.parse(path.read_text())
 found=[n.value for n in ast.walk(tree) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id==target for t in n.targets)]
 if target=='values':found=[v for v in found if any(isinstance(n,ast.Call) and isinstance(n.func,ast.Attribute) and n.func.attr=='findall' for n in ast.walk(v))]
 assert len(found)==1,(path.name,target,len(found))
 return compile(ast.Expression(found[0]),str(path),'eval')
def evaluate(path,target,env):return eval(expression(path,target),{'re':re,'float':float},env)
def main():
 old=ROOT.parent/'minimal-scene-two-rr-prepared-v1'
 prior=old/'two_rr_scene_receipt.py';current=ROOT/'two_rr_scene_receipt.py';scene=ROOT/'scene_input_receipt.py'
 schedule=ROOT/'host/phase_schedule.inl'
 assert sha(schedule)==sha(old/'host/phase_schedule.inl')
 assert 'phase_J[32]={' in schedule.read_text() and 'phase_J[3]={' not in schedule.read_text()
 try:evaluate(prior,'schedule_text',{'ROOT':old})
 except IndexError:old_failed=True
 else:raise AssertionError('actual old receipt expression must fail before selecting a frame')
 parsed=evaluate(current,'schedule_text',{'ROOT':ROOT})
 scene_parsed=evaluate(scene,'schedule',{'root':ROOT})
 assert parsed==scene_parsed
 assert len(re.findall(r'\{([^}]+)\}',parsed))==32
 rows=[]
 for frame in range(3):
  values=evaluate(current,'values',{'schedule_text':parsed,'i':frame})
  scene_values=evaluate(scene,'values',{'schedule':scene_parsed,'frame':frame})
  assert values==scene_values
  j=evaluate(current,'j',{'values':values})
  # The actual tuple assignment in scene_input_receipt supplies sx,sy.
  tree=ast.parse(scene.read_text())
  nodes=[n.value for n in ast.walk(tree) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Tuple) and [x.id for x in t.elts]==['sx','sy'] for t in n.targets)]
  assert len(nodes)==1
  sx,sy=eval(compile(ast.Expression(nodes[0]),str(scene),'eval'),{'float':float},{'values':scene_values})
  assert (sx,sy)==(-j[0],-j[1])
  constants=struct.pack('<4I8f',1280,720,frame,1,sx,sy,1,1280/720,0,1,-4,.375)
  rows.append({'frame':frame,'producer_s':[sx,sy],'RR_jitter':j,'oracle_transport':[-j[0],-j[1]],'same_actual_parser_values':True,'scene_constants_bytes':len(constants),'scene_constants_sha256':hashlib.sha256(constants).hexdigest()})
 return {'CPU':True,'GPU':False,'NGX':False,'old_actual_parser_error':'IndexError','old_actual_marker':'phase_J[3]={','new_actual_marker':'phase_J[32]={','frozen_array_rows':32,'observed_prefix_rows_checked':3,'both_actual_receipt_schedule_expressions_equal':True,'all_s_equals_negative_j':True,'schedule_source_sha256':sha(schedule),'old_receipt_sha256':sha(prior),'new_receipt_sha256':sha(current),'scene_input_receipt_sha256':sha(scene),'frames':rows}
if __name__=='__main__':
 result=main();(ROOT/'CPU_PHASE32_PARSER.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result,indent=2))
