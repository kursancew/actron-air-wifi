from twisted.web import server, resource
from twisted.internet.protocol import DatagramProtocol
from twisted.internet import reactor
import jinja2
import time
import enum


POLL_INT = 1
TEMPLATES = '.'
jinjaenv = jinja2.Environment(loader=jinja2.FileSystemLoader(TEMPLATES))


class ControlState(enum.Enum):
    OFF = 0
    COOL = 1
    FAN = 2
    WARM = 3
    
class UnitState(enum.Enum):
    OFF = 0
    IDLE = 1
    FAN = 2
    RUN_COOL = 3
    RUN_WARM = 4
    HISTERISIS = 5
    
    
def from_proto_state(v):
    d = {'h': UnitState.HISTERISIS,
         's': UnitState.OFF,
         'i': UnitState.IDLE,
         'f': UnitState.FAN_ONLY,
         'c': UnitState.RUN_COOL,
         'w': UnitState.RUN_WARM,
         }
    return d[v]


class Control(object):
    def __init__(self):
        self.target_state = ControlState.OFF
        self.target_zones = [False, False, False, False]
        self.enabled_zones = [True, False, False, False]

        self.unit_zones = [False, False, False, False]
        self.unit_state = UnitState.OFF
        
        self.target_temperature = 24
        self.sensor_temperature = 24
        
        self.evaluate_def = reactor.callLater(3, self.evaluate)
        self.last_evaluate = 0

    def cool_enough(self):
        return self.sensor_temperature < self.target_temperature

    def dump(self):
        r = (
            ('Comfort Temperature', self.target_temperature),
            ('Current Temperature', self.sensor_temperature),
            ('Unit state', self.unit_state.name),
            ('Control selection', self.target_state.name),
            )
        return r

    def _evaluate_unit_off(self):
        if self.target_state == ControlState.COOL:
            if self.cool_enough():
                self.actions.fan()
            else:
                self.actions.power()
        elif self.target_state == ControlState.COOL:
            pass
        elif self.target_state == ControlState.FAN:
            self.actions.fan()
        
    def _evaluate_unit_histerisis(self):
        pass
        
    def _evaluate_unit_idle(self):
        if self.target_state in (ControlState.OFF, ControlState.FAN):
            self.actions.power()
        
    def _evaluate_unit_fan_only(self):
        if self.target_state == ControlState.OFF:
            self.actions.power()
        elif self.target_state == ControlState.COOL and not self.cool_enough():
            self.actions.power()
        
    def _evaluate_unit_run_cool(self):
        if self.target_state == ControlState.OFF:
            self.actions.power()
        elif self.target_state == ControlState.COOL and self.cool_enough():
            self.actions.power()
        elif self.target_state == ControlState.FAN:
            self.actions.power()
        
    def _evaluate_unit_run_warm(self):
        pass

    def evaluate(self):
        cll = '_evaluate_unit_' + self.unit_state.name.lower()
        meth = getattr(self, cll)
        meth()
        if self.evaluate_def.active():
            self.evaluate_def.reset(10)
        else:
            self.evaluate_def = reactor.callLater(10, self.evaluate)
        
    def set_target_temperature(self, temp):
        self.target_temperature = temp
        
    def set_sensor_temperature(self, temp, sid):
        self.sensor_temperature = temp
        
    def set_unit_state(self, s):
        self.unit_state = s
        
    def set_unit_zones(self, z):
        self.unit_zones = z
        
    def set_target_state(self, s):
        self.target_state = s


class WallController(DatagramProtocol):
    def __init__(self, controller_addr, control):
        self.controller_addr = controller_addr
        self.control = control
        self.control.actions = self
        self.last_action = {}

    def chk_action(self, act):
        if time.time() - self.last_action.setdefault(act, 0) < 3:
            print('TOO EARLY FOR', act)
            return false
        print(act)
        self.last_action = time.time()
        return true
    
    def fan(self):
        if self.chk_action('FAN'):
            self.transport.write(b'f')
        
    def zone(self, z):
        if self.chk_action('ZONE' + str(z)):
            self.transport.write(str(z).encode('ascii'))
        
    def power(self):
        if self.chk_action('POWER'):
            self.transport.write(b'p')
    
    def startProtocol(self):
        self.transport.connect(*self.controller_addr)
        self.poll()
        
    def poll(self):
        self.transport.write(b'q')
        reactor.callLater(POLL_INT, self.poll)
        
    def datagramReceived(self, data, addr):
        host, port = addr
        #print "received %r from %s:%d" % (data, host, port)
        data = data.decode('ascii')
        self.state = data[0]
        from_proto_state(data[0])
        zones = [ (x == '1') for x in data[1:5] ]
        self.control.set_unit_zones()


class Sensor(DatagramProtocol):
    def __init__(self, sensor_addr, control):
        self.sensor_addr = sensor_addr
        self.read = 0
        self.control = control
        
    def startProtocol(self):
        self.transport.connect(*self.sensor_addr)
        self.poll()
        
    def poll(self):
        self.transport.write(b'q')
        reactor.callLater(POLL_INT, self.poll)
        
    def datagramReceived(self, data, addr):
        if data[:1] == b't':
            data = data[1:data.index(0)]
            temperature = float(data.decode('ascii'))
            

def reqarg(request, a):
    a = a.encode('ascii')
    if a in request.args:
        return request.args[a][0].decode('ascii')
    return None


class WebIf(resource.Resource):
    isLeaf = True
    
    def render_GET(self, request):
        templ = jinjaenv.get_template('index.html')
        print(request.args)
        set_mode = reqarg(request, 'set_mode')
        if set_mode:
            control.set_target_state(ControlState[set_mode.upper()])
        set_temp = reqarg(request, 'temp')
        if set_temp:
            control.set_target_temperature(int(set_temp))
        return templ.render(data=control.dump()).encode('ascii')
        


site = server.Site(WebIf())
control = Control()
reactor.listenTCP(8080, site)
reactor.listenUDP(0, Sensor(('192.168.1.151', 7001), control))
reactor.listenUDP(0, WallController(('192.168.1.150', 7000), control))
reactor.run()