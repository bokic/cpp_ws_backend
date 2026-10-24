import { Routes } from '@angular/router';
import { SetupComponent } from './pages/setup/setup.component';
import { LoginComponent } from './pages/login/login.component';
import { BoardComponent } from './pages/board/board.component';
import { GanttComponent } from './pages/gantt/gantt.component';
import { CreateItemComponent } from './pages/create-item/create-item.component';
import { ItemDetailComponent } from './pages/item-detail/item-detail.component';
import { WorkflowsComponent } from './pages/workflows/workflows.component';

export const routes: Routes = [
  { path: '', pathMatch: 'full', redirectTo: 'board' },
  { path: 'setup', component: SetupComponent },
  { path: 'login', component: LoginComponent },
  { path: 'board', component: BoardComponent },
  { path: 'gantt', component: GanttComponent },
  { path: 'create', component: CreateItemComponent },
  { path: 'items/:id', component: ItemDetailComponent },
  { path: 'workflows', component: WorkflowsComponent },
  { path: '**', redirectTo: 'board' },
];
