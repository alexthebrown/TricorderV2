import tkinter as tk
import tkinter.ttk as ttk
import tkinter.messagebox as msg

class Mainframe(ttk.Frame):
    # Mainframe contains the widgets
    # More advanced programs may have multiple frames
    # or possibly a grid of subframes
    
    def __init__(self,master,*args,**kwargs):
        # *args packs positional arguments into tuple args
        #  **kwargs packs keyword arguments into dict kwargs
        
        # initialise base class
        ttk.Frame.__init__(self,master,*args,**kwargs)
        # using super means if you change the base class 
        # you do not have to change this line
        # in this case the * an ** operators unpack the parameters
        
        # you will put your widgets here
        ttk.Button(self,text = 'Quit',width = 25,command = master.destroy).pack()
        

class App(tk.Tk):
    def __init__(self):
        tk.Tk.__init__(self)
               
        # set the title bar text
        self.title('Tricorder')
        # Make sure app window is big enough to show title 
        self.geometry('320x240')
      
        # create a menu bar
        # if you do not need menus discard the following code
        mainMenu = tk.Menu(self)
        self.config(menu=mainMenu)
        
        # create a file menu with an exit entry
        # you may need to add more entries
        fileMenu = tk.Menu(mainMenu)
        fileMenu.add_command(label='Exit',command=self.destroy)
        mainMenu.add_cascade(label='File',menu=fileMenu)
        
        # any main menu should have a help entry
        helpMenu = tk.Menu(mainMenu)
        helpMenu.add_command(label = 'about',command = self.showAbout)
        mainMenu.add_cascade(label = 'Help',menu = helpMenu)
        
        # create and pack a Mainframe window
        Mainframe(self).pack()
        
    def showAbout(self):
        # show the about box
        msg.showinfo('About', 'This is a template Python/tkinter app')
            
# create and run an App object
App().mainloop()